// mouse_release_gate.h - run something only after the mouse button-up.
//
// Use femmAfterMouseRelease() for EVERY modal dialog that a mouse press or
// double-click asks for (including item-view itemDoubleClicked slots). Never
// call exec() / QMessageBox::information() etc. from inside the handler, and
// do not "fix" it with a 0 ms timer -- see mouse_release_gate.cpp.
#pragma once
#include <functional>
class QObject;

// Runs fn on the event loop once the OS has delivered the button-up for the
// mouse event being handled now; immediately (still queued) if no button is
// held. fn is dropped if context is destroyed first.
void femmAfterMouseRelease(QObject *context, std::function<void()> fn);
