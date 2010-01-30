/*
 * Hydrogen
 * Copyright(c) 2002-2008 by Alex >Comix< Cominu [comix@users.sourceforge.net]
 *
 * http://www.hydrogen-music.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "DrumPatternEditor.h"
#include "PatternEditorPanel.h"
#include "NotePropertiesRuler.h"

#include <hydrogen/globals.h>
#include <hydrogen/Song.h>
#include <hydrogen/hydrogen.h>
#include <hydrogen/Preferences.h>
#include <hydrogen/event_queue.h>
#include <hydrogen/instrument.h>
#include <hydrogen/Pattern.h>
#include <hydrogen/note.h>
#include <hydrogen/audio_engine.h>

#include "UndoActions.h"
#include "../HydrogenApp.h"
#include "../Mixer/Mixer.h"
#include "../Skin.h"

#include <math.h>
#include <cassert>
#include <algorithm>

#include <QtGui>

using namespace std;
using namespace H2Core;

DrumPatternEditor::DrumPatternEditor(QWidget* parent, PatternEditorPanel *panel)
 : QWidget( parent )
 , Object( "DrumPatternEditor" )
 , m_nResolution( 8 )
 , m_bUseTriplets( false )
 , m_bRightBtnPressed( false )
 , m_pDraggedNote( NULL )
 , m_pPattern( NULL )
 , m_pPatternEditorPanel( panel )
{
	//setAttribute(Qt::WA_NoBackground);
	setFocusPolicy(Qt::ClickFocus);

	m_nGridWidth = Preferences::get_instance()->getPatternEditorGridWidth();
	m_nGridHeight = Preferences::get_instance()->getPatternEditorGridHeight();

	unsigned nEditorWidth = 20 + m_nGridWidth * ( MAX_NOTES * 4 );
	m_nEditorHeight = m_nGridHeight * MAX_INSTRUMENTS;

	resize( nEditorWidth, m_nEditorHeight );

	HydrogenApp::get_instance()->addEventListener( this );
	
}



DrumPatternEditor::~DrumPatternEditor()
{
}



void DrumPatternEditor::updateEditor()
{
	Hydrogen* engine = Hydrogen::get_instance();

	// check engine state
	int state = engine->getState();
	if ( (state != STATE_READY) && (state != STATE_PLAYING) ) {
		ERRORLOG( "FIXME: skipping pattern editor update (state shoud be READY or PLAYING)" );
		return;
	}

	Hydrogen *pEngine = Hydrogen::get_instance();
	PatternList *pPatternList = pEngine->getSong()->get_pattern_list();
	int nSelectedPatternNumber = pEngine->getSelectedPatternNumber();
	if ( (nSelectedPatternNumber != -1) && ( (uint)nSelectedPatternNumber < pPatternList->get_size() ) ) {
		m_pPattern = pPatternList->get( nSelectedPatternNumber );
	}
	else {
		m_pPattern = NULL;
	}
	__selectedPatternNumber = nSelectedPatternNumber;


	uint nEditorWidth;
	if ( m_pPattern ) {
		nEditorWidth = 20 + m_nGridWidth * m_pPattern->get_length();
	}
	else {
		nEditorWidth = 20 + m_nGridWidth * MAX_NOTES;
	}
	resize( nEditorWidth, height() );

	// redraw all
	update( 0, 0, width(), height() );
}



int DrumPatternEditor::getColumn(QMouseEvent *ev)
{
	int nBase;
	if (m_bUseTriplets) {
		nBase = 3;
	}
	else {
		nBase = 4;
	}
	int nWidth = (m_nGridWidth * 4 * MAX_NOTES) / (nBase * m_nResolution);

	int x = ev->x();
	int nColumn;
	nColumn = x - 20 + (nWidth / 2);
	nColumn = nColumn / nWidth;
	nColumn = (nColumn * 4 * MAX_NOTES) / (nBase * m_nResolution);
	return nColumn;
}



void DrumPatternEditor::mousePressEvent(QMouseEvent *ev)
{
	if ( m_pPattern == NULL ) {
		return;
	}
	Song *pSong = Hydrogen::get_instance()->getSong();
	int nInstruments = pSong->get_instrument_list()->get_size();

	int row = (int)( ev->y()  / (float)m_nGridHeight);
	if (row >= nInstruments) {
		return;
	}

	int nColumn = getColumn( ev );

	if ( nColumn >= (int)m_pPattern->get_length() ) {
		update( 0, 0, width(), height() );
		return;
	}
	Instrument *pSelectedInstrument = pSong->get_instrument_list()->get( row );

	if (ev->button() == Qt::LeftButton ) {

		unsigned nRealColumn = 0;
			if( ev->x() > 20 ) {
				nRealColumn = (ev->x() - 20) / static_cast<float>(m_nGridWidth);
			}

		H2Core::Note *pDraggedNote;
		std::multimap <int, Note*>::iterator pos;
		for ( pos = m_pPattern->note_map.lower_bound( nColumn ); pos != m_pPattern->note_map.upper_bound( nColumn ); ++pos ) {
			Note *pNote = pos->second;
			assert( pNote );
	
			if ( pNote->get_instrument() == pSelectedInstrument ) {
				pDraggedNote = pNote;
				break;
			}
		}
		if ( !pDraggedNote ) {
			for ( pos = m_pPattern->note_map.lower_bound( nRealColumn ); pos != m_pPattern->note_map.upper_bound( nRealColumn ); ++pos ) {
				Note *pNote = pos->second;
				assert( pNote );
	
				if ( pNote->get_instrument() == pSelectedInstrument ) {
					pDraggedNote = pNote;
					break;
				}
			}
		}

		int oldLength = -1;
		float oldVelocity = 0.8f;
		float oldPan_L = 0.5f;
		float oldPan_R = 0.5f;
		float oldLeadLag = 0.0f;
		int oldNoteKeyVal = 0;
		int oldOctaveKeyVal = 0;

		if( pDraggedNote ){
			oldLength = pDraggedNote->get_length();
			oldVelocity = pDraggedNote->get_velocity();
			oldPan_L = pDraggedNote->get_pan_l();
			oldPan_R = pDraggedNote->get_pan_r();
			oldLeadLag = pDraggedNote->get_leadlag();
			oldNoteKeyVal = pDraggedNote->m_noteKey.m_key;
			oldOctaveKeyVal = pDraggedNote->m_noteKey.m_nOctave;
		}

		SE_addNoteAction *action = new SE_addNoteAction( nColumn,
								 row,
								 __selectedPatternNumber,
								 oldLength,
								 oldVelocity,
								 oldPan_L,
								 oldPan_R,
								 oldLeadLag,
								 oldNoteKeyVal,
								 oldOctaveKeyVal );
		HydrogenApp::get_instance()->m_undoStack->push( action );
	}
	else if (ev->button() == Qt::RightButton ) {
	
		unsigned nRealColumn = 0;
			if( ev->x() > 20 ) {
				nRealColumn = (ev->x() - 20) / static_cast<float>(m_nGridWidth);
			}
		m_bRightBtnPressed = true;
		m_pDraggedNote = NULL;
	
		//	__rightclickedpattereditor
		//	0 = note length
		//	1 = note off"
		//	2 = edit velocity
		//	3 = edit pan
		//	4 = edit lead lag

		if ( Preferences::get_instance()->__rightclickedpattereditor == 1){
			SE_addNoteRightClickAction *action = new SE_addNoteRightClickAction( nColumn, row, __selectedPatternNumber );
			HydrogenApp::get_instance()->m_undoStack->push( action );
			return;
		}

//		AudioEngine::get_instance()->lock( RIGHT_HERE );
	
		std::multimap <int, Note*>::iterator pos;
		for ( pos = m_pPattern->note_map.lower_bound( nColumn ); pos != m_pPattern->note_map.upper_bound( nColumn ); ++pos ) {
			Note *pNote = pos->second;
			assert( pNote );
	
			if ( pNote->get_instrument() == pSelectedInstrument ) {
				m_pDraggedNote = pNote;
				break;
			}
		}
		if ( !m_pDraggedNote ) {
			for ( pos = m_pPattern->note_map.lower_bound( nRealColumn ); pos != m_pPattern->note_map.upper_bound( nRealColumn ); ++pos ) {
				Note *pNote = pos->second;
				assert( pNote );
	
				if ( pNote->get_instrument() == pSelectedInstrument ) {
					m_pDraggedNote = pNote;
					break;
				}
			}
		}

		// potrei essere sulla coda di una nota precedente..
		for ( int nCol = 0; unsigned(nCol) < nRealColumn; ++nCol ) {
			if ( m_pDraggedNote ) break;
			for ( pos = m_pPattern->note_map.lower_bound( nCol ); pos != m_pPattern->note_map.upper_bound( nCol ); ++pos ) {
				Note *pNote = pos->second;
				assert( pNote );
	
				if ( pNote->get_instrument() == pSelectedInstrument
				&& ( (nRealColumn <= pNote->get_position() + pNote->get_length() )
				&& nRealColumn >= pNote->get_position() ) ){
					m_pDraggedNote = pNote;
					break;
				}
			}
		}
		
		//needed for undo note length
		__nRealColumn = nRealColumn;
		__nColumn = nColumn;
		__row = row;
		if( m_pDraggedNote ){
			__oldLength = m_pDraggedNote->get_length();
		}else
		{
			__oldLength = -1;
		}
		
//		AudioEngine::get_instance()->unlock();
	}
}

void DrumPatternEditor::addOrDeleteNoteAction(  int nColumn,
						int row,
						int selectedPatternNumber,
						int oldLength,
						float oldVelocity,
						float oldPan_L,
						float oldPan_R,
						float oldLeadLag,
						int oldNoteKeyVal,
						int oldOctaveKeyVal )
{

	Hydrogen *pEngine = Hydrogen::get_instance();
	PatternList *pPatternList = pEngine->getSong()->get_pattern_list();
	H2Core::Pattern *pPattern;

	if ( ( selectedPatternNumber != -1 ) && ( (uint)selectedPatternNumber < pPatternList->get_size() ) ) {
		pPattern = pPatternList->get( selectedPatternNumber );
	}
	else {
		pPattern = NULL;
	}


	Song *pSong = Hydrogen::get_instance()->getSong();
	int nInstruments = pSong->get_instrument_list()->get_size();

	Instrument *pSelectedInstrument = pSong->get_instrument_list()->get( row );
	m_bRightBtnPressed = false;

	AudioEngine::get_instance()->lock( RIGHT_HERE );	// lock the audio engine

	bool bNoteAlreadyExist = false;
	std::multimap <int, Note*>::iterator pos;
	for ( pos = pPattern->note_map.lower_bound( nColumn ); pos != pPattern->note_map.upper_bound( nColumn ); ++pos ) {
		Note *pNote = pos->second;
		assert( pNote );
		if ( pNote->get_instrument() == pSelectedInstrument ) {
			// the note exists...remove it!
			bNoteAlreadyExist = true;
			delete pNote;
			pPattern->note_map.erase( pos );
			break;
		}
	}

	if ( bNoteAlreadyExist == false ) {
		// create the new note
		const unsigned nPosition = nColumn;
		const float fVelocity = oldVelocity;
		const float fPan_L = oldPan_L ;
		const float fPan_R = oldPan_R;
		int nLength = oldLength;

		const float fPitch = 0.0f;
		Note *pNote = new Note( pSelectedInstrument, nPosition, fVelocity, fPan_L, fPan_R, nLength, fPitch );
		pNote->set_noteoff( false );
		pNote->set_leadlag( oldLeadLag );

		if ( oldNoteKeyVal == 0 ){//note c
			pNote->m_noteKey.m_key = H2Core::NoteKey::C;
		}
		if ( oldNoteKeyVal == 1 ){//note cis / cs
			pNote->m_noteKey.m_key = H2Core::NoteKey::Cs;
		}
		if ( oldNoteKeyVal == 2 ){//note d
			pNote->m_noteKey.m_key = H2Core::NoteKey::D;
		}
		if ( oldNoteKeyVal == 3 ){//note dis / ef
			pNote->m_noteKey.m_key = H2Core::NoteKey::Ef;
		}
		if ( oldNoteKeyVal == 4 ){//note E
			pNote->m_noteKey.m_key = H2Core::NoteKey::E;
		}
		if ( oldNoteKeyVal == 5 ){//note f
			pNote->m_noteKey.m_key = H2Core::NoteKey::F;
		}
		if ( oldNoteKeyVal == 6 ){//note fis
			pNote->m_noteKey.m_key = H2Core::NoteKey::Fs;
		}
		if ( oldNoteKeyVal == 7 ){//note g
			pNote->m_noteKey.m_key = H2Core::NoteKey::G;
		}
		if ( oldNoteKeyVal == 8 ){//note gis / af
			pNote->m_noteKey.m_key = H2Core::NoteKey::Af;
		}
		if ( oldNoteKeyVal == 9 ){//note a
			pNote->m_noteKey.m_key = H2Core::NoteKey::A;
		}
		if ( oldNoteKeyVal == 10 ){//note his / bf
			pNote->m_noteKey.m_key = H2Core::NoteKey::Bf;
		}
		if ( oldNoteKeyVal == 11 ){//note h / b
			pNote->m_noteKey.m_key = H2Core::NoteKey::B;
		}

		pNote->m_noteKey.m_nOctave = oldOctaveKeyVal;
		

		pPattern->note_map.insert( std::make_pair( nPosition, pNote ) );

		// hear note
		Preferences *pref = Preferences::get_instance();
		if ( pref->getHearNewNotes() ) {
			Note *pNote2 = new Note( pSelectedInstrument, 0, fVelocity, fPan_L, fPan_R, nLength, fPitch);
			AudioEngine::get_instance()->get_sampler()->note_on(pNote2);
		}
	}
	pSong->__is_modified = true;
	AudioEngine::get_instance()->unlock(); // unlock the audio engine

	// update the selected line
	int nSelectedInstrument = Hydrogen::get_instance()->getSelectedInstrumentNumber();
	if (nSelectedInstrument != row) {
		Hydrogen::get_instance()->setSelectedInstrumentNumber( row );
	}
	else {
		update( 0, 0, width(), height() );
		m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
		m_pPatternEditorPanel->getPanEditor()->updateEditor();
		m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
		m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
		m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();
	}
}


void DrumPatternEditor::addNoteRightClickAction( int nColumn, int row, int selectedPatternNumber )
{

	Hydrogen *pEngine = Hydrogen::get_instance();
	PatternList *pPatternList = pEngine->getSong()->get_pattern_list();

	H2Core::Pattern *pPattern;
	if ( (selectedPatternNumber != -1) && ( (uint)selectedPatternNumber < pPatternList->get_size() ) ) {
		pPattern = pPatternList->get( selectedPatternNumber );
	}
	else {
		pPattern = NULL;
	}

	Song *pSong = Hydrogen::get_instance()->getSong();
	int nInstruments = pSong->get_instrument_list()->get_size();
	Instrument *pSelectedInstrument = pSong->get_instrument_list()->get( row );


	m_bRightBtnPressed = true;
	m_pDraggedNote = NULL;

	AudioEngine::get_instance()->lock( RIGHT_HERE );

	// create the new note
	const unsigned nPosition = nColumn;
	const float fVelocity = 0.0f;
	const float fPan_L = 0.5f;
	const float fPan_R = 0.5f;
	const int nLength = 1;
	const float fPitch = 0.0f;
	Note *poffNote = new Note( pSelectedInstrument, nPosition, fVelocity, fPan_L, fPan_R, nLength, fPitch);
	poffNote->set_noteoff( true );

	
	pPattern->note_map.insert( std::make_pair( nPosition, poffNote ) );

	pSong->__is_modified = true;

	AudioEngine::get_instance()->unlock();

	// update the selected line
	int nSelectedInstrument = Hydrogen::get_instance()->getSelectedInstrumentNumber();
	if (nSelectedInstrument != row) {
		Hydrogen::get_instance()->setSelectedInstrumentNumber( row );
	}
	else {
		update( 0, 0, width(), height() );
		m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
		m_pPatternEditorPanel->getPanEditor()->updateEditor();
		m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
		m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
		m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();
	}
}


void DrumPatternEditor::mouseReleaseEvent(QMouseEvent *ev)
{
	UNUSED( ev );
	setCursor( QCursor( Qt::ArrowCursor ) );

	if (m_pPattern == NULL) {
		return;
	}

	if (m_bRightBtnPressed && m_pDraggedNote && ( Preferences::get_instance()->__rightclickedpattereditor == 0 ) ) {
		if ( m_pDraggedNote->get_noteoff() ) return;

		SE_editNoteLenghtAction *action = new SE_editNoteLenghtAction( m_pDraggedNote->get_position(),  m_pDraggedNote->get_position(), __row, m_pDraggedNote->get_length(),__oldLength, __selectedPatternNumber);
		HydrogenApp::get_instance()->m_undoStack->push( action );
	}
}


void DrumPatternEditor::editNoteLenghtAction( int nColumn, int nRealColumn, int row, int length, int selectedPatternNumber )
{
	Hydrogen *pEngine = Hydrogen::get_instance();
	PatternList *pPatternList = pEngine->getSong()->get_pattern_list();

	H2Core::Pattern *pPattern;
	if ( (selectedPatternNumber != -1) && ( (uint)selectedPatternNumber < pPatternList->get_size() ) ) {
		pPattern = pPatternList->get( selectedPatternNumber );
	}
	else {
		pPattern = NULL;
	}

	Note *pDraggedNote;
	Song *pSong = pEngine->getSong();
	int nInstruments = pSong->get_instrument_list()->get_size();

	Instrument *pSelectedInstrument = pSong->get_instrument_list()->get( row );

	AudioEngine::get_instance()->lock( RIGHT_HERE );

	std::multimap <int, Note*>::iterator pos;
	for ( pos = pPattern->note_map.lower_bound( nColumn ); pos != pPattern->note_map.upper_bound( nColumn ); ++pos ) {
		Note *pNote = pos->second;
		assert( pNote );

		if ( pNote->get_instrument() == pSelectedInstrument ) {
			pDraggedNote = pNote;
			break;
		}
	}
	if ( !pDraggedNote ) {
		for ( pos = pPattern->note_map.lower_bound( nRealColumn ); pos != pPattern->note_map.upper_bound( nRealColumn ); ++pos ) {
			Note *pNote = pos->second;
			assert( pNote );

			if ( pNote->get_instrument() == pSelectedInstrument ) {
				pDraggedNote = pNote;
				break;
			}
		}


	}
	// potrei essere sulla coda di una nota precedente..
	for ( int nCol = 0; unsigned(nCol) < nRealColumn; ++nCol ) {
		if ( pDraggedNote ) break;
		for ( pos = pPattern->note_map.lower_bound( nCol ); pos != pPattern->note_map.upper_bound( nCol ); ++pos ) {
			Note *pNote = pos->second;
			assert( pNote );

			if ( pNote->get_instrument() == pSelectedInstrument
			&& ( (nRealColumn <= pNote->get_position() + pNote->get_length() )
			&& nRealColumn >= pNote->get_position() ) ){
				pDraggedNote = pNote;
				break;
			}
		}
	}

	pDraggedNote->set_length( length );
	AudioEngine::get_instance()->unlock();
	update( 0, 0, width(), height() );
	m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
	m_pPatternEditorPanel->getPanEditor()->updateEditor();
	m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
	m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();

}



void DrumPatternEditor::mouseMoveEvent(QMouseEvent *ev)
{
	if (m_pPattern == NULL) {
		return;
	}

	int row = MAX_INSTRUMENTS - 1 - (ev->y()  / (int)m_nGridHeight);
	if (row >= MAX_INSTRUMENTS) {
		return;
	}

	//	__rightclickedpattereditor
	//	0 = note length
	//	1 = note off"
	//	2 = edit velocity
	//	3 = edit pan
	//	4 = edit lead lag

	if (m_bRightBtnPressed && m_pDraggedNote && ( Preferences::get_instance()->__rightclickedpattereditor == 0 ) ) {
		if ( m_pDraggedNote->get_noteoff() ) return;
		int nTickColumn = getColumn( ev );

		AudioEngine::get_instance()->lock( RIGHT_HERE );	// lock the audio engine
		int nLen = nTickColumn - (int)m_pDraggedNote->get_position();

		if (nLen <= 0) {
			nLen = -1;
		}

		float fNotePitch = m_pDraggedNote->m_noteKey.m_nOctave * 12 + m_pDraggedNote->m_noteKey.m_key;
		float fStep = 0;
		if(nLen > -1){
			fStep = pow( 1.0594630943593, ( double )fNotePitch );
		}else
		{
			fStep = 1.0; 
		}
		m_pDraggedNote->set_length( nLen * fStep);

		Hydrogen::get_instance()->getSong()->__is_modified = true;
		AudioEngine::get_instance()->unlock(); // unlock the audio engine

		//__draw_pattern();
		update( 0, 0, width(), height() );
		m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
		m_pPatternEditorPanel->getPanEditor()->updateEditor();
		m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
		m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	}

}



void DrumPatternEditor::keyPressEvent (QKeyEvent *ev)
{
	ev->ignore();
}



///
/// Draws a pattern
///
void DrumPatternEditor::__draw_pattern(QPainter& painter)
{
	const UIStyle *pStyle = Preferences::get_instance()->getDefaultUIStyle();
	const QColor selectedRowColor( pStyle->m_patternEditor_selectedRowColor.getRed(), pStyle->m_patternEditor_selectedRowColor.getGreen(), pStyle->m_patternEditor_selectedRowColor.getBlue() );

	__create_background( painter );

	if (m_pPattern == NULL) {
		return;
	}

	int nNotes = m_pPattern->get_length();
	int nSelectedInstrument = Hydrogen::get_instance()->getSelectedInstrumentNumber();
	Song *pSong = Hydrogen::get_instance()->getSong();

	InstrumentList * pInstrList = pSong->get_instrument_list();

	
	if ( m_nEditorHeight != (int)( m_nGridHeight * pInstrList->get_size() ) ) {
		// the number of instruments is changed...recreate all
		m_nEditorHeight = m_nGridHeight * pInstrList->get_size();
		resize( width(), m_nEditorHeight );
	}

	for ( uint nInstr = 0; nInstr < pInstrList->get_size(); ++nInstr ) {
		uint y = m_nGridHeight * nInstr;
		if ( nInstr == (uint)nSelectedInstrument ) {	// selected instrument
			painter.fillRect( 0, y + 1, ( 20 + nNotes * m_nGridWidth ), m_nGridHeight - 1, selectedRowColor );
		}
	}


	// draw the grid
	__draw_grid( painter );
	

	/*
		BUGFIX
		
		if m_pPattern is not renewed every time we draw a note, 
		hydrogen will crash after you save a song and create a new one. 
		-smoors
	*/
	Hydrogen *pEngine = Hydrogen::get_instance();
	PatternList *pPatternList = pEngine->getSong()->get_pattern_list();
	int nSelectedPatternNumber = pEngine->getSelectedPatternNumber();
	if ( (nSelectedPatternNumber != -1) && ( (uint)nSelectedPatternNumber < pPatternList->get_size() ) ) {
		m_pPattern = pPatternList->get( nSelectedPatternNumber );
	}
	else {
		m_pPattern = NULL;
	}
	// ~ FIX



	if( m_pPattern->note_map.size() == 0) return;

	std::multimap <int, Note*>::iterator pos;
	for ( pos = m_pPattern->note_map.begin(); pos != m_pPattern->note_map.end(); pos++ ) {
		Note *note = pos->second;
		assert( note );
		__draw_note( note, painter );
	}
}


///
/// Draws a note
///
void DrumPatternEditor::__draw_note( Note *note, QPainter& p )
{
	static const UIStyle *pStyle = Preferences::get_instance()->getDefaultUIStyle();
	static const QColor noteColor( pStyle->m_patternEditor_noteColor.getRed(), pStyle->m_patternEditor_noteColor.getGreen(), pStyle->m_patternEditor_noteColor.getBlue() );
	static const QColor noteoffColor( pStyle->m_patternEditor_noteoffColor.getRed(), pStyle->m_patternEditor_noteoffColor.getGreen(), pStyle->m_patternEditor_noteoffColor.getBlue() );

	p.setRenderHint( QPainter::Antialiasing );

	int nInstrument = -1;
	InstrumentList * pInstrList = Hydrogen::get_instance()->getSong()->get_instrument_list();
	for ( uint nInstr = 0; nInstr < pInstrList->get_size(); ++nInstr ) {
		Instrument *pInstr = pInstrList->get( nInstr );
		if ( pInstr == note->get_instrument() ) {
 			nInstrument = nInstr;
			break;
		}
	}
	if ( nInstrument == -1 ) {
		ERRORLOG( "Instrument not found..skipping note" );
		return;
	}

	uint pos = note->get_position();

	p.setPen( noteColor );

	int red = (int) (note->get_velocity() * 255);
	int green;
	int blue;
	blue = (255 - (int) red)* .33;
	green =  (255 - (int) red);

	uint w = 8;
	uint h =  m_nGridHeight / 3;

	if ( note->get_length() == -1 && note->get_noteoff() == false ) {	// trigger note
		uint x_pos = 20 + (pos * m_nGridWidth);// - m_nGridWidth / 2.0;
		uint y_pos = ( nInstrument * m_nGridHeight) + (m_nGridHeight / 2) - 3;
		p.setBrush(QColor( red,green,blue ));
		p.drawEllipse( x_pos -4 , y_pos, w, h );


	}
	else if ( note->get_length() == 1 && note->get_noteoff() == true ){
		p.setPen( noteoffColor );
		uint x_pos = 20 + ( pos * m_nGridWidth );// - m_nGridWidth / 2.0;

		uint y_pos = ( nInstrument * m_nGridHeight ) + (m_nGridHeight / 2) - 3;
		p.setBrush(QColor( noteoffColor));
		p.drawEllipse( x_pos -4 , y_pos, w, h );



	}		
	else {
		float fNotePitch = note->m_noteKey.m_nOctave * 12 + note->m_noteKey.m_key;
		float fStep = pow( 1.0594630943593, ( double )fNotePitch );

		uint x = 20 + (pos * m_nGridWidth);
		int w = m_nGridWidth * note->get_length() / fStep;
		w = w - 1;	// lascio un piccolo spazio tra una nota ed un altra

		int y = (int) ( ( nInstrument ) * m_nGridHeight  + (m_nGridHeight / 100.0 * 30.0) );
		int h = (int) (m_nGridHeight - ((m_nGridHeight / 100.0 * 30.0) * 2.0) );
		p.setBrush(QColor( red,green,blue ));
		p.fillRect( x, y + 1, w, h + 1, QColor( red,green,blue ) );	/// \todo: definire questo colore nelle preferenze
		p.drawRect( x, y + 1, w, h + 1 );
	}
}




void DrumPatternEditor::__draw_grid( QPainter& p )
{
	static const UIStyle *pStyle = Preferences::get_instance()->getDefaultUIStyle();
	static const QColor res_1( pStyle->m_patternEditor_line1Color.getRed(), pStyle->m_patternEditor_line1Color.getGreen(), pStyle->m_patternEditor_line1Color.getBlue() );
	static const QColor res_2( pStyle->m_patternEditor_line2Color.getRed(), pStyle->m_patternEditor_line2Color.getGreen(), pStyle->m_patternEditor_line2Color.getBlue() );
	static const QColor res_3( pStyle->m_patternEditor_line3Color.getRed(), pStyle->m_patternEditor_line3Color.getGreen(), pStyle->m_patternEditor_line3Color.getBlue() );
	static const QColor res_4( pStyle->m_patternEditor_line4Color.getRed(), pStyle->m_patternEditor_line4Color.getGreen(), pStyle->m_patternEditor_line4Color.getBlue() );
	static const QColor res_5( pStyle->m_patternEditor_line5Color.getRed(), pStyle->m_patternEditor_line5Color.getGreen(), pStyle->m_patternEditor_line5Color.getBlue() );

	// vertical lines
	p.setPen( QPen( res_1, 0, Qt::DotLine ) );

	int nBase;
	if (m_bUseTriplets) {
		nBase = 3;
	}
	else {
		nBase = 4;
	}

	int n4th = 4 * MAX_NOTES / (nBase * 4);
	int n8th = 4 * MAX_NOTES / (nBase * 8);
	int n16th = 4 * MAX_NOTES / (nBase * 16);
	int n32th = 4 * MAX_NOTES / (nBase * 32);
	int n64th = 4 * MAX_NOTES / (nBase * 64);

	int nNotes = MAX_NOTES;
	if ( m_pPattern ) {
		nNotes = m_pPattern->get_length();
	}
	if (!m_bUseTriplets) {
		for ( int i = 0; i < nNotes + 1; i++ ) {
			uint x = 20 + i * m_nGridWidth;

			if ( (i % n4th) == 0 ) {
				if (m_nResolution >= 4) {
					p.setPen( QPen( res_1, 0 ) );
					p.drawLine(x, 1, x, m_nEditorHeight - 1);
				}
			}
			else if ( (i % n8th) == 0 ) {
				if (m_nResolution >= 8) {
					p.setPen( QPen( res_2, 0 ) );
					p.drawLine(x, 1, x, m_nEditorHeight - 1);
				}
			}
			else if ( (i % n16th) == 0 ) {
				if (m_nResolution >= 16) {
					p.setPen( QPen( res_3, 0 ) );
					p.drawLine(x, 1, x, m_nEditorHeight - 1);
				}
			}
			else if ( (i % n32th) == 0 ) {
				if (m_nResolution >= 32) {
					p.setPen( QPen( res_4, 0 ) );
					p.drawLine(x, 1, x, m_nEditorHeight - 1);
				}
			}
			else if ( (i % n64th) == 0 ) {
				if (m_nResolution >= 64) {
					p.setPen( QPen( res_5, 0 ) );
					p.drawLine(x, 1, x, m_nEditorHeight - 1);
				}
			}
		}
	}
	else {	// Triplets
		uint nCounter = 0;
		int nSize = 4 * MAX_NOTES / (nBase * m_nResolution);

		for ( int i = 0; i < nNotes + 1; i++ ) {
			uint x = 20 + i * m_nGridWidth;

			if ( (i % nSize) == 0) {
				if ((nCounter % 3) == 0) {
					p.setPen( QPen( res_1, 0 ) );
				}
				else {
					p.setPen( QPen( res_3, 0 ) );
				}
				p.drawLine(x, 1, x, m_nEditorHeight - 1);
				nCounter++;
			}
		}
	}


	// fill the first half of the rect with a solid color
	static const QColor backgroundColor( pStyle->m_patternEditor_backgroundColor.getRed(), pStyle->m_patternEditor_backgroundColor.getGreen(), pStyle->m_patternEditor_backgroundColor.getBlue() );
	static const QColor selectedRowColor( pStyle->m_patternEditor_selectedRowColor.getRed(), pStyle->m_patternEditor_selectedRowColor.getGreen(), pStyle->m_patternEditor_selectedRowColor.getBlue() );
	int nSelectedInstrument = Hydrogen::get_instance()->getSelectedInstrumentNumber();
	Song *pSong = Hydrogen::get_instance()->getSong();
	int nInstruments = pSong->get_instrument_list()->get_size();
	for ( uint i = 0; i < (uint)nInstruments; i++ ) {
		uint y = m_nGridHeight * i + 1;
		if ( i == (uint)nSelectedInstrument ) {
			p.fillRect( 0, y, (20 + nNotes * m_nGridWidth), (int)( m_nGridHeight * 0.7 ), selectedRowColor );
		}
		else {
			p.fillRect( 0, y, (20 + nNotes * m_nGridWidth), (int)( m_nGridHeight * 0.7 ), backgroundColor );
		}
	}

}


void DrumPatternEditor::__create_background( QPainter& p)
{
	static const UIStyle *pStyle = Preferences::get_instance()->getDefaultUIStyle();
	static const QColor backgroundColor( pStyle->m_patternEditor_backgroundColor.getRed(), pStyle->m_patternEditor_backgroundColor.getGreen(), pStyle->m_patternEditor_backgroundColor.getBlue() );
	static const QColor alternateRowColor( pStyle->m_patternEditor_alternateRowColor.getRed(), pStyle->m_patternEditor_alternateRowColor.getGreen(), pStyle->m_patternEditor_alternateRowColor.getBlue() );
	static const QColor lineColor( pStyle->m_patternEditor_lineColor.getRed(), pStyle->m_patternEditor_lineColor.getGreen(), pStyle->m_patternEditor_lineColor.getBlue() );

	int nNotes = MAX_NOTES;
	if ( m_pPattern ) {
		nNotes = m_pPattern->get_length();
	}

	Song *pSong = Hydrogen::get_instance()->getSong();
	int nInstruments = pSong->get_instrument_list()->get_size();

	if ( m_nEditorHeight != (int)( m_nGridHeight * nInstruments ) ) {
		// the number of instruments is changed...recreate all
		m_nEditorHeight = m_nGridHeight * nInstruments;
		resize( width(), m_nEditorHeight );
	}

	p.fillRect(0, 0, 20 + nNotes * m_nGridWidth, height(), backgroundColor);
	for ( uint i = 0; i < (uint)nInstruments; i++ ) {
		uint y = m_nGridHeight * i;
		if ( ( i % 2) != 0) {
			p.fillRect( 0, y, (20 + nNotes * m_nGridWidth), m_nGridHeight, alternateRowColor );
		}
	}

	// horizontal lines
	p.setPen( lineColor );
	for ( uint i = 0; i < (uint)nInstruments; i++ ) {
		uint y = m_nGridHeight * i + m_nGridHeight;
		p.drawLine( 0, y, (20 + nNotes * m_nGridWidth), y);
	}

	p.drawLine( 0, m_nEditorHeight, (20 + nNotes * m_nGridWidth), m_nEditorHeight );
}



void DrumPatternEditor::paintEvent( QPaintEvent* /*ev*/ )
{
	//INFOLOG( "paint" );
	//QWidget::paintEvent(ev);
	
	QPainter painter( this );
	__draw_pattern( painter );
}






void DrumPatternEditor::showEvent ( QShowEvent *ev )
{
	UNUSED( ev );
	updateEditor();
}



void DrumPatternEditor::hideEvent ( QHideEvent *ev )
{
	UNUSED( ev );
}



void DrumPatternEditor::setResolution(uint res, bool bUseTriplets)
{
	this->m_nResolution = res;
	this->m_bUseTriplets = bUseTriplets;

	// redraw all
	update( 0, 0, width(), height() );
	m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
	m_pPatternEditorPanel->getPanEditor()->updateEditor();
	m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
	m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	/// \todo [DrumPatternEditor::setResolution] aggiornare la risoluzione del Ruler in alto."
}



void DrumPatternEditor::zoom_in()
{
	if (m_nGridWidth >= 3){
		m_nGridWidth *= 2;
	}else
	{
		m_nGridWidth *= 1.5;
	}
	updateEditor();
}



void DrumPatternEditor::zoom_out()
{
	if ( m_nGridWidth > 1.5 ) {
		if (m_nGridWidth > 3){
			m_nGridWidth /= 2;
		}else
		{
			m_nGridWidth /= 1.5;
		}
		updateEditor();
	}
}

void DrumPatternEditor::selectedInstrumentChangedEvent()
{
	update( 0, 0, width(), height() );
}


/// This method is called from another thread (audio engine)
void DrumPatternEditor::patternModifiedEvent()
{
	update( 0, 0, width(), height() );
}


void DrumPatternEditor::patternChangedEvent()
{
	updateEditor();
}


void DrumPatternEditor::selectedPatternChangedEvent()
{
	//cout << "selected pattern changed EVENT" << endl;
	updateEditor();
}


///NotePropertiesRuler undo redo action
void DrumPatternEditor::undoRedoAction( int column,
					QString mode,
					int nSelectedPatternNumber,
					int nSelectedInstrument,
					float velocity,
					float pan_L,
					float pan_R,
					float leadLag,
					int noteKeyVal,
					int octaveKeyVal)
{
	Hydrogen *pEngine = Hydrogen::get_instance();
	Song *pSong = pEngine->getSong();
	Pattern *pPattern;
	PatternList *pPatternList = pEngine->getSong()->get_pattern_list();
	if ( (nSelectedPatternNumber != -1) && ( (uint)nSelectedPatternNumber < pPatternList->get_size() ) ) {
		pPattern = pPatternList->get( nSelectedPatternNumber );
	}
	else {
		pPattern = NULL;
	}

	std::multimap <int, Note*>::iterator pos;
	for ( pos = pPattern->note_map.lower_bound( column ); pos != pPattern->note_map.upper_bound( column ); ++pos ) {
		Note *pNote = pos->second;
		assert( pNote );
		assert( (int)pNote->get_position() == column );
		if ( pNote->get_instrument() != pSong->get_instrument_list()->get( nSelectedInstrument ) ) {
			continue;
		}

		if ( mode == "VELOCITY" && !pNote->get_noteoff() ) {
			pNote->set_velocity( velocity );
		}
		else if ( mode == "PAN" ){

			pNote->set_pan_l( pan_L );
			pNote->set_pan_r( pan_R );
		}
		else if ( mode == "LEADLAG" ){
			pNote->set_leadlag( leadLag );
		}
		else if ( mode == "NOTEKEY" ){
	
			if ( noteKeyVal == 0 ){//note c
				pNote->m_noteKey.m_key = H2Core::NoteKey::C;
			}
			if ( noteKeyVal == 1 ){//note cis / cs
				pNote->m_noteKey.m_key = H2Core::NoteKey::Cs;
			}
			if ( noteKeyVal == 2 ){//note d
				pNote->m_noteKey.m_key = H2Core::NoteKey::D;
			}
			if ( noteKeyVal == 3 ){//note dis / ef
				pNote->m_noteKey.m_key = H2Core::NoteKey::Ef;
			}
			if ( noteKeyVal == 4 ){//note E
				pNote->m_noteKey.m_key = H2Core::NoteKey::E;
			}
			if ( noteKeyVal == 5 ){//note f
				pNote->m_noteKey.m_key = H2Core::NoteKey::F;
			}
			if ( noteKeyVal == 6 ){//note fis
				pNote->m_noteKey.m_key = H2Core::NoteKey::Fs;
			}
			if ( noteKeyVal == 7 ){//note g
				pNote->m_noteKey.m_key = H2Core::NoteKey::G;
			}
			if ( noteKeyVal == 8 ){//note gis / af
				pNote->m_noteKey.m_key = H2Core::NoteKey::Af;
			}
			if ( noteKeyVal == 9 ){//note a
				pNote->m_noteKey.m_key = H2Core::NoteKey::A;
			}
			if ( noteKeyVal == 10 ){//note his / bf
				pNote->m_noteKey.m_key = H2Core::NoteKey::Bf;
			}
			if ( noteKeyVal == 11 ){//note h / b
				pNote->m_noteKey.m_key = H2Core::NoteKey::B;
			}
	
				pNote->m_noteKey.m_nOctave = octaveKeyVal;
			}
		pSong->__is_modified = true;
		break;
	}
	updateEditor();
	m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
	m_pPatternEditorPanel->getPanEditor()->updateEditor();
	m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
	m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();

}


///==========================================================
///undo / redo actions from pattern editor instrument list

void DrumPatternEditor::functionClearNotesRedoAction( int nSelectedInstrument, int patternNumber )
{
	Hydrogen * H = Hydrogen::get_instance();
	PatternList *pPatternList = Hydrogen::get_instance()->getSong()->get_pattern_list();
	Pattern *pPattern = pPatternList->get( patternNumber );

	Instrument *pSelectedInstrument = H->getSong()->get_instrument_list()->get( nSelectedInstrument );

	pPattern->purge_instrument( pSelectedInstrument );
	EventQueue::get_instance()->push_event( EVENT_SELECTED_INSTRUMENT_CHANGED, -1 );
}



void DrumPatternEditor::functionClearNotesUndoAction( std::list< H2Core::Note* > noteList, int nSelectedInstrument, int patternNumber )
{
	Hydrogen * H = Hydrogen::get_instance();
	PatternList *pPatternList = Hydrogen::get_instance()->getSong()->get_pattern_list();
	Pattern *pPattern = pPatternList->get( patternNumber );

	std::list < H2Core::Note *>::iterator pos;
	for ( pos = noteList.begin(); pos != noteList.end(); ++pos){
		Note *pNote;
		pNote = new Note(*pos);
		assert( pNote );
		int nPosition = pNote->get_position();
		pPattern->note_map.insert( std::make_pair( nPosition, pNote ) );
	}
	EventQueue::get_instance()->push_event( EVENT_SELECTED_INSTRUMENT_CHANGED, -1 );
	updateEditor();
	m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
	m_pPatternEditorPanel->getPanEditor()->updateEditor();
	m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
	m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();

}



void DrumPatternEditor::functionFillNotesUndoAction( QStringList noteList, int nSelectedInstrument, int patternNumber )
{
	Hydrogen * H = Hydrogen::get_instance();
	PatternList *pPatternList = Hydrogen::get_instance()->getSong()->get_pattern_list();
	Pattern *pPattern = pPatternList->get( patternNumber );
	Instrument *pSelectedInstrument = H->getSong()->get_instrument_list()->get( nSelectedInstrument );

	AudioEngine::get_instance()->lock( RIGHT_HERE );	// lock the audio engine

	for (int i = 0; i < noteList.size(); i++ ) {
		int nColumn  = noteList.value(i).toInt();
		std::multimap <int, Note*>::iterator pos;
		for ( pos = pPattern->note_map.lower_bound( nColumn ); pos != pPattern->note_map.upper_bound( nColumn ); ++pos ) {
			Note *pNote = pos->second;
			assert( pNote );
			if ( pNote->get_instrument() == pSelectedInstrument ) {
				// the note exists...remove it!
				delete pNote;
				pPattern->note_map.erase( pos );
				break;
			}
		}
	}
	AudioEngine::get_instance()->unlock();	// unlock the audio engine

	EventQueue::get_instance()->push_event( EVENT_SELECTED_INSTRUMENT_CHANGED, -1 );
	updateEditor();
	m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
	m_pPatternEditorPanel->getPanEditor()->updateEditor();
	m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
	m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();
}


void DrumPatternEditor::functionFillNotesRedoAction( QStringList noteList, int nSelectedInstrument, int patternNumber )
{
	Hydrogen * H = Hydrogen::get_instance();
	PatternList *pPatternList = Hydrogen::get_instance()->getSong()->get_pattern_list();
	Pattern *pPattern = pPatternList->get( patternNumber );
	Instrument *pSelectedInstrument = H->getSong()->get_instrument_list()->get( nSelectedInstrument );

	const float velocity = 0.8f;
	const float pan_L = 0.5f;
	const float pan_R = 0.5f;
	const float fPitch = 0.0f;
	const int nLength = -1;

	AudioEngine::get_instance()->lock( RIGHT_HERE );	// lock the audio engine
	for (int i = 0; i < noteList.size(); i++ ) {

		// create the new note
		int position = noteList.value(i).toInt();
		Note *pNote = new Note( pSelectedInstrument, position, velocity, pan_L, pan_R, nLength, fPitch );
		pPattern->note_map.insert( std::make_pair( position, pNote ) );	
	}
	AudioEngine::get_instance()->unlock();	// unlock the audio engine

	EventQueue::get_instance()->push_event( EVENT_SELECTED_INSTRUMENT_CHANGED, -1 );
	updateEditor();
	m_pPatternEditorPanel->getVelocityEditor()->updateEditor();
	m_pPatternEditorPanel->getPanEditor()->updateEditor();
	m_pPatternEditorPanel->getLeadLagEditor()->updateEditor();
	m_pPatternEditorPanel->getNoteKeyEditor()->updateEditor();
	m_pPatternEditorPanel->getPianoRollEditor()->updateEditor();
}

///~undo / redo actions from pattern editor instrument list
///==========================================================
