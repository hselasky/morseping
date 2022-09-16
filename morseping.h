/*-
 * Copyright (c) 2015 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MORSEPING_H_
#define	_MORSEPING_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include <sys/filio.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/soundcard.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <arpa/inet.h>

#include <sysexits.h>

#include <errno.h>
#include <err.h>

#include <signal.h>

#include <QApplication>
#include <QPushButton>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QColor>
#include <QGroupBox>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QIcon>

class MPMainWindow;

#define	MP_BUFFER_MS_MAX	(2 * 128)
#define	MP_SAMPLE_RATE		(8000)

class MPGridLayout : public QWidget, public QGridLayout
{
public:
	MPGridLayout() : QGridLayout(this) { };
	~MPGridLayout() { };
};

class MPGroupBox : public QGroupBox, public QGridLayout
{
public:
	MPGroupBox(const QString &title) : QGroupBox(title),
	    QGridLayout(this) { };
	~MPGroupBox() { };
};

class MPVolume : public QWidget
{
	Q_OBJECT;

public:
	MPVolume(QWidget *parent = 0);
	~MPVolume();

	int curr_pos;
	int curr_delta;
	int min; /* exclusive */
	int mid;
	int max; /* inclusive */
	int focus;
	int moving;
	int y_pos;

	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);

	void setRange(int,int,int);
	int value(void) const;
	void setValue(int);

	void enterEvent(QEvent *);
	void leaveEvent(QEvent *);

	void paintEvent(QPaintEvent *);

signals:
	void valueChanged(int);
};

class MPMainWindow : public QWidget
{
	Q_OBJECT
public:
	MPMainWindow();
	~MPMainWindow();

	void set_config_ok();
	void set_config_fail();

	uint8_t zstart[0];

	MPGroupBox *gb_config;
	MPGroupBox *gb_morseping;
	QGridLayout *gl_main;
	QLineEdit *led_dsp;
	QLineEdit *led_gw;
	MPVolume *spn_freq;
	MPVolume *spn_volume;
	QPushButton *but_apply;
	QPushButton *but_morse;
	QLabel *lbl_config;

	QTimer *timer;

	pthread_t worker;

	int configure;
	int doit;
	int freq;
	int amp;
	int sendsocket;
	int recvsocket;
	int audiofd;
	int last[3];
	int rem;
	int error;
	int last_sample;
	int buffer[MP_BUFFER_MS_MAX];
	int bufpos;
	char dsp[256];
	char gw[256];
	uint8_t packet[IP_MAXPACKET] __attribute__((__aligned__(4)));
	uint16_t seq;
	int low_pass_1;
	int low_pass_2;
	int send_state;
	
	struct msghdr recv_msg;
	struct iovec recv_iov;
	struct sockaddr_in send_to;
	struct sockaddr_in recv_from;

	uint8_t zend[0];

public slots:
	void handle_config_apply();
	void handle_freq_apply(int);
	void handle_amp_apply(int);
	void handle_morse_on();
	void handle_morse_off();
	void handle_config_ok();
	void handle_config_fail();
signals:
	void send_config_ok();
	void send_config_fail();
};

#endif		/* _MORSEPING_H_ */
