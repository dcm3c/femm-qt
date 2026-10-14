// mouse_release_gate.cpp -- see the header for the rule; this is the why.
//
// Qt discards mouse events aimed at a window that a modal dialog is blocking,
// and does so WITHOUT updating QGuiApplication::mouseButtons(). A modal
// dialog opened from a mouse press or double-click handler therefore runs
// its event loop before the OS has delivered that click's button-up
// (~70-130 ms after the press): the button-up is dropped, Qt believes the
// button held forever, delivers every later press (toolbar buttons draw
// pressed, clicks in the drawing still create nodes) and discards every later
// release (nothing ever clicks). That was the macOS "toolbar buttons stay
// depressed after viewing properties" report. Windows' platform plugin
// re-syncs button state from the OS on each event and self-heals; cocoa does
// not. Queuing the dialog with QTimer::singleShot(0) does NOT help -- it
// still runs before the button-up (measured). Only the release itself does.
#include "mouse_release_gate.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>

namespace {
class ReleaseGate : public QObject {
public:
    ReleaseGate(QObject *ctx, std::function<void()> fn)
        : m_ctx(ctx), m_fn(std::move(fn)) {
        QCoreApplication::instance()->installEventFilter(this);
        // A button-up that never arrives (synthetic input, focus stolen) must
        // not strand the dialog: run anyway after a second.
        QTimer::singleShot(1000, this, [this] { fire(); });
    }
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(e)->buttons() == Qt::NoButton) {
            // Seen here BEFORE dispatch. Let it be delivered, then run.
            QTimer::singleShot(0, this, [this] { fire(); });
        }
        return false;
    }
private:
    void fire() {
        if (m_fired) return;
        m_fired = true;
        QCoreApplication::instance()->removeEventFilter(this);
        if (m_ctx) m_fn();
        deleteLater();
    }
    QPointer<QObject> m_ctx;
    std::function<void()> m_fn;
    bool m_fired = false;
};
} // namespace

void femmAfterMouseRelease(QObject *context, std::function<void()> fn)
{
    if (QGuiApplication::mouseButtons() == Qt::NoButton) {
        QTimer::singleShot(0, context, std::move(fn));   // never inside the handler
        return;
    }
    new ReleaseGate(context, std::move(fn));
}
