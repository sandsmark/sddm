/***************************************************************************
* Copyright (c) 2013 Abdurrahman AVCI <abdurrahmanavci@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
***************************************************************************/

#include "DisplayServer.h"

#include "Configuration.h"
#include "DaemonApp.h"
#include "Display.h"

#include <QDebug>
#include <QProcess>

#include <xcb/xcb.h>

#include <unistd.h>

namespace SDDM {
    DisplayServer::DisplayServer(QObject *parent) : QObject(parent) {
    }

    DisplayServer::~DisplayServer() {
// TODO: free wayland structures
        stop();
    }

    void DisplayServer::setDisplay(const QString &display) {
        m_display = display;
    }

    void DisplayServer::setAuthPath(const QString &authPath) {
        m_authPath = authPath;
    }

    void DisplayServer::switchVtCallback(void *data, struct wl_display_manager *display_manager, uint32_t vt) {
        qDebug() << "Switching to virtual terminal" << vt;
        int console = open("/dev/console", O_RDONLY | O_NOCTTY);
        ioctl(console, VT_ACTIVATE, vt);
        close(console);
    }

    void DisplayServer::waylandCallback(wl_display *display, uint32_t id, const char *interface, uint32_t version, void *data) {
        DisplayServer *this = data;
        qDebug() << "Wayland compositor ready!";
        this->m_wlDisplayManager = wl_display_bind (display, id, &wl_display_manager_interface);

        wl_display_manager_listener listener = { switchVtCallback };
        wl_display_manager_add_listener (this->m_wlDisplayManager, &listener, this);

        /* Handle keys for VT switching */
        // 1 | 2 == MODIFIER_CTRL | MODIFIER_ALT
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F1, 1 | 2, 1);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F2, 1 | 2, 2);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F3, 1 | 2, 3);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F4, 1 | 2, 4);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F5, 1 | 2, 5);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F6, 1 | 2, 6);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F7, 1 | 2, 7);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F8, 1 | 2, 8);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F9, 1 | 2, 9);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F10, 1 | 2, 10);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F11, 1 | 2, 11);
        wl_display_manager_bind_key (seat->priv->display_manager, KEY_F11, 1 | 2, 12);
        this->m_launchWait.unlock();
    }

    bool DisplayServer::start() {
        // check flag
        if (m_started)
            return false;

        // create process
        process = new QProcess(this);

        // delete process on finish
        connect(process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(finished()));

        // log message
        qDebug() << " DAEMON: Display server starting...";

        // get display
        Display *display = qobject_cast<Display *>(parent());

        // Get socket for communication
        int sockets[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socket_fds);
        int westonFd = fcntl(sockets[0], F_DUPFD, 0);
        close(sockets[0]);

        // start display server
        process->start("weston", { "--shell=system-compositor.so", QString("--display-manager-fd=%1").arg(westonFd), QString("--tty=%1").arg(display->terminalId())});
        close(westonFd);

        // wait for display server to start
        if (!process->waitForStarted()) {
            // log message
            qCritical() << " DAEMON: Failed to start display server process.";

            // return fail
            return false;
        }

        m_waylandConnection = wl_display_connect(NULL);
        wl_display_add_global_listener(m_waylandConnection, waylandCallback, this);

        // wait until we can connect to the display server
        m_launchWaiter.lock();
        if (!m_launchWaiter.tryLock(10000)) {
            // log message
            qCritical() << " DAEMON: Failed to connect to the display server.";

            // return fail
            return false;
        }

        // log message
        qDebug() << " DAEMON: Display server started.";

        // set flag
        m_started = true;

        // return success
        return true;
    }

    void DisplayServer::stop() {
        // check flag
        if (!m_started)
            return;

        // log message
        qDebug() << " DAEMON: Display server stopping...";

        // terminate process
        process->terminate();

        // wait for finished
        if (!process->waitForFinished(5000))
            process->kill();
    }

    void DisplayServer::finished() {
        // check flag
        if (!m_started)
            return;

        // reset flag
        m_started = false;

        // log message
        qDebug() << " DAEMON: Display server stopped.";

        // clean up
        process->deleteLater();
        process = nullptr;

        // emit signal
        emit stopped();
    }

}
