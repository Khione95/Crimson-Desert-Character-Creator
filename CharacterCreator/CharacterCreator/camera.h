#pragma once

// The preview camera while the editor is open: the character stands left of
// the panel and R switches between the whole body and the face (the mouse
// still turns the camera).
//
// It uses the game's own camera settings (Accessibility: Camera Distance,
// Vertical and Horizontal Offset), changed in memory while the editor is open
// and put back when it closes. The player's settings file is never touched.

// The editor opened for a character (their gender picks the framing).
void CameraMenuOpened(int ch);
void CameraMenuClosed();

// Face <-> whole body.
void CameraToggleView();
