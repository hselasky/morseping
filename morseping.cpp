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

#include "morseping.h"

static MPMainWindow *mw;
static pthread_mutex_t mp_mutex;

static void
mp_lock(void)
{
	pthread_mutex_lock(&mp_mutex);
}

static void
mp_unlock(void)
{
	pthread_mutex_unlock(&mp_mutex);
}

/* This function was copied from FreeBSD's userland ping utility */
static uint16_t
mp_in_cksum(const uint16_t *addr, int len)
{
	int nleft, sum;
	const uint16_t *w;
	union {
		uint16_t	us;
		u_char	uc[2];
	}     last;
	uint16_t answer;

	nleft = len;
	sum = 0;
	w = addr;

	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}

	if (nleft == 1) {
		last.uc[0] = *(u_char *)w;
		last.uc[1] = 0;
		sum += last.us;
	}
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}

static void
mp_send(int num)
{
	struct icmp icp;
	int x;

	for (x = 0; x != num; x++) {
		uint16_t temp = mw->seq++;
		memset(&icp, 0, sizeof(icp));
		icp.icmp_type = ICMP_ECHO;
		icp.icmp_seq = htons(temp);
		icp.icmp_id = htons(0xdead + x);
		icp.icmp_cksum =
		  mp_in_cksum((const uint16_t *)&icp, ICMP_MINLEN);

		send(mw->sendsocket, &icp, ICMP_MINLEN, 0);
	}
}

static int16_t
mp_subtract_safe(int16_t a, int16_t b)
{
	int32_t temp = a - b;

	if (temp < -0x7FFF)
		temp = -0x7FFF;
	else if (temp > 0x7FFF)
		temp = 0x7FFF;
	return temp;
}


static void
mp_recv(void)
{
	struct ip *ip;
	struct icmp *icp;
	int cc;
	int hlen;
	int value;
	int delta;

	while (1) {
		mw->recv_iov.iov_base = mw->packet;
		mw->recv_iov.iov_len = IP_MAXPACKET;

		memset(&mw->recv_msg, 0, sizeof(mw->recv_msg));
		mw->recv_msg.msg_name = &mw->recv_from;
		mw->recv_msg.msg_namelen = sizeof(mw->recv_from);
		mw->recv_msg.msg_iov = &mw->recv_iov;
		mw->recv_msg.msg_iovlen = 1;

		cc = recvmsg(mw->recvsocket, &mw->recv_msg, 0);
		if (cc < 0)
			break;

		/* decode packet */
		ip = (struct ip *)mw->packet;
		hlen = ip->ip_hl << 2;
		icp = (struct icmp *)(mw->packet + hlen);

		/* check if packet was for us */
		if (icp->icmp_type == ICMP_ECHOREPLY &&
		    icp->icmp_id == htons(0xdead) &&
		    bcmp(&mw->recv_from, &mw->send_to, sizeof(mw->recv_from)) == 0) {
			value = ntohs(ip->ip_id);
			delta = (int16_t)(value - mw->last[0]);
			mw->last[0] = value;
			value = delta - mw->last[1];
			mw->last[1] = delta;
			if (mw->bufpos < MP_BUFFER_MS_MAX)
				mw->buffer[mw->bufpos++] = value;
		}
	}
}

static int16_t
mp_hp_f1(int16_t in)
{
	mw->low_pass_1 += (((int32_t)(in) * (1<<8)) - (mw->low_pass_1 / (1<<8)));
	return mp_subtract_safe(in, (mw->low_pass_1 / (1<<16)));
}

static int16_t
mp_hp_f2(int16_t in)
{
	mw->low_pass_2 += (((int32_t)(in) * (1<<8)) - (mw->low_pass_2 / (1<<8)));
	return mp_subtract_safe(in, (mw->low_pass_2 / (1<<16)));
}

static void
mp_generate_audio(void)
{
	int16_t buffer[MP_BUFFER_MS_MAX / 2];
	int x;
	int y;

	if (ioctl(mw->audiofd, SNDCTL_DSP_GETODELAY, &x) == 0 &&
	    x >= (int)sizeof(buffer))
		return;

	for (x = y = 0; x != MP_BUFFER_MS_MAX / 2; x++) {
		mw->rem += mw->freq;
		if (mw->rem >= MP_SAMPLE_RATE) {
			mw->rem -= MP_SAMPLE_RATE;
			while (mw->bufpos - y > (mw->freq / 50) + 2)
				y++;
			if (y < mw->bufpos) {
				mw->last_sample =
				    mp_subtract_safe(mw->buffer[y] * mw->amp, 0);
				y++;
			}
		}
		mw->last_sample = mp_hp_f1(mp_hp_f2(mw->last_sample));
		buffer[x] = mw->last_sample;
	}
	if (write(mw->audiofd, buffer, sizeof(buffer)) < 0)
		mw->configure = 1;
	for (x = 0; y != mw->bufpos; x++, y++)
		mw->buffer[x] = mw->buffer[y];
	mw->bufpos = x;
}

static int
mp_config_audio(void)
{
	int fd;
	int val;

	fd = open(mw->dsp, O_WRONLY);
	if (fd < 0)
		goto error;

	val = 0;
	if (ioctl(fd, FIONBIO, &val) < 0)
		goto error;

	val = AFMT_S16_NE;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &val) < 0)
		goto error;
	val = 1;
	if (ioctl(fd, SOUND_PCM_WRITE_CHANNELS, &val) < 0)
		goto error;

	val = MP_SAMPLE_RATE;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &val) < 0)
		goto error;

	val = 0x00020008;
	if (ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &val) < 0)
		goto error;

	return (fd);
error:
	close(fd);
	return (-1);
}

static int
mp_config_send(void)
{
	int bufsize = sizeof(mw->packet);
	int fd;

	memset(&mw->send_to, 0, sizeof(mw->send_to));
	mw->send_to.sin_family = AF_INET;
	mw->send_to.sin_len = sizeof(mw->send_to);
	if (inet_aton(mw->gw, &mw->send_to.sin_addr) != 1)
		return (-1);

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (fd < 0)
		return (fd);
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
	    (char *)&bufsize, sizeof(bufsize));

	if (connect(fd, (struct sockaddr *)&mw->send_to,
	    sizeof(mw->send_to)) != 0) {
		close(fd);
		fd = -1;
	}
	return (fd);
}

static int
mp_config_recv(void)
{
	int bufsize = sizeof(mw->packet);
	int fd;
	int val;

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (fd < 0)
		return (fd);
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
	    (char *)&bufsize, sizeof(bufsize));

	val = 1;
	if (ioctl(fd, FIONBIO, &val) < 0) {
		close(fd);
		return (-1);
	}
	return (fd);
}

static void *
mp_process(void *arg)
{
	struct kevent event[2];
	struct timespec ts = { 1, 0 };
	int g = -1;
	int n;
	int x;

	mp_lock();
	while (1) {
		if (mw->configure) {
			close(g);
			if (mw->configure == 1) {
				close(mw->sendsocket);
				close(mw->recvsocket);
				close(mw->audiofd);

				mw->sendsocket = mp_config_send();
				mw->recvsocket = mp_config_recv();
				mw->audiofd = mp_config_audio();
			}
			if (mw->sendsocket < 0 ||
			    mw->recvsocket < 0 ||
			    mw->audiofd < 0)
				g = -1;
			else
				g = kqueue();

			if (g > -1) {
				memset(event, 0, sizeof(event));
				EV_SET(&event[0], mw->recvsocket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
				kevent(g, event, 1, NULL, 0, NULL);
				EV_SET(&event[0], 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, (1000 / mw->freq) + 1 , 0);
				kevent(g, event, 1, NULL, 0, NULL);
			}
			mw->configure = 0;
			mw->bufpos = 0;
			if (g < 0) {
				mw->set_config_fail();
				mw->error = errno;
			} else {
				mw->set_config_ok();
			}
		}
		mp_unlock();
		if (g < 0) {
			usleep(1000000);
			mp_lock();
			continue;
		}
		n = kevent(g, NULL, 0, event, 2, &ts);

		mp_lock();
		for (x = 0; x < n; x++) {
			if (event[x].filter == EVFILT_READ)
				mp_recv();
			if (event[x].filter == EVFILT_TIMER) {
				mp_send(mw->doit ? mw->send_state : 1);
				mw->send_state ++;
				mw->send_state %= 3;
				mp_generate_audio();
			}
		}
	}
	return (NULL);
}

MPMainWindow :: MPMainWindow()
{
	mw = this;
	pthread_mutex_init(&mp_mutex, NULL);

	memset(zstart, 0, zend - zstart);
	
	gb_config = new MPGroupBox(tr("Configuration"));
	gb_morseping = new MPGroupBox(tr("MorsePing"));
	gl_main = new QGridLayout(this);
	led_dsp = new QLineEdit();
	led_gw = new QLineEdit();
	spn_freq = new MPVolume();
	spn_freq->setRange(1,1000,150);
	spn_volume = new MPVolume();
	spn_volume->setRange(0,0x7FFF,256);
	but_apply = new QPushButton(tr("Apply config"));
	but_morse = new QPushButton(tr("Press here\nto send morse\nsignal"));
	lbl_config = new QLabel();
	timer = new QTimer();
	doit = 0;
	freq = 50;
	amp = 768;
	sendsocket = -1;
	recvsocket = -1;
	audiofd = -1;
	strcpy(dsp, "/dev/dsp");
	strcpy(gw, "127.0.0.1");

	led_dsp->setText(QString(dsp));
	led_gw->setText(QString(gw));
	spn_freq->setValue(freq);
	spn_volume->setValue(amp);

	gb_config->addWidget(new QLabel(tr("DSP device:")), 0,0,1,1);
	gb_config->addWidget(new QLabel(tr("Gateway")), 1,0,1,1);
	gb_config->addWidget(led_dsp, 0,1,1,1);
	gb_config->addWidget(led_gw, 1,1,1,1);
	gb_config->addWidget(but_apply,2,0,1,2);
	gb_config->addWidget(lbl_config,3,0,1,2);

	gb_morseping->addWidget(but_morse, 0,0,2,1);
	gb_morseping->addWidget(new QLabel(tr("Frequency")), 0,1,1,1);
	gb_morseping->addWidget(new QLabel(tr("Volume")), 1,1,1,1);
	gb_morseping->addWidget(spn_freq, 0,2,1,1);
	gb_morseping->addWidget(spn_volume, 1,2,1,1);

	gl_main->addWidget(gb_config, 0,0,1,1);
	gl_main->addWidget(gb_morseping, 1,0,1,1);
	
	connect(but_apply, SIGNAL(released()), this, SLOT(handle_config_apply()));
	connect(but_morse, SIGNAL(pressed()), this, SLOT(handle_morse_on()));
	connect(but_morse, SIGNAL(released()), this, SLOT(handle_morse_off()));
	connect(spn_freq, SIGNAL(valueChanged(int)), this, SLOT(handle_freq_apply(int)));
	connect(spn_volume, SIGNAL(valueChanged(int)), this, SLOT(handle_amp_apply(int)));
	connect(this, SIGNAL(send_config_fail()), this, SLOT(handle_config_fail()));
	connect(this, SIGNAL(send_config_ok()), this, SLOT(handle_config_ok()));

	pthread_create(&worker, NULL, &mp_process, NULL);

	setWindowIcon(QIcon(QString(":/MorsePing.png")));

	show();
}

MPMainWindow :: ~MPMainWindow()
{
}

void
MPMainWindow :: handle_config_ok()
{
	lbl_config->setText(tr("Configuration OK"));
}

void
MPMainWindow :: handle_config_fail()
{
	lbl_config->setText(tr("Configuration failed: %1").arg(QString(strerror(mw->error))));
}

void
MPMainWindow :: set_config_ok()
{
	send_config_ok();
}

void
MPMainWindow :: set_config_fail()
{
	send_config_fail();
}

void
MPMainWindow :: handle_config_apply()
{
	mp_lock();
	strncpy(dsp, led_dsp->text().toLatin1().data(), sizeof(dsp));
	dsp[sizeof(dsp)-1] = 0;
	strncpy(gw, led_gw->text().toLatin1().data(), sizeof(gw));
	gw[sizeof(gw)-1] = 0;
	configure = 1;
	mp_unlock();
}

void
MPMainWindow :: handle_freq_apply(int value)
{
	mp_lock();
	freq = value;
	if (mw->audiofd > -1 && configure == 0)
		configure = 2;
	mp_unlock();
}

void
MPMainWindow :: handle_amp_apply(int value)
{
	mp_lock();
	amp = value;
	mp_unlock();
}

void
MPMainWindow :: handle_morse_on()
{
	mp_lock();
	doit = 1;
	mp_unlock();
}

void
MPMainWindow :: handle_morse_off()
{
	mp_lock();
	doit = 0;
	mp_unlock();
}

MPVolume :: MPVolume(QWidget *parent)
  : QWidget(parent)
{
	y_pos = moving = focus = curr_delta =
	    curr_pos = min = max = 0;
	mid = 1;

	setMinimumSize(QSize(150,150));
	setMaximumSize(QSize(150,150));
}

MPVolume :: ~MPVolume()
{

}

void
MPVolume :: mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		moving = 1;
		curr_delta = 0;
		y_pos = event->y();
	}
}

void
MPVolume :: mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		moving = 0;
		y_pos = 0;
		curr_pos += curr_delta;
		curr_delta = 0;

		emit valueChanged(curr_pos);
		update();
	}
}

void
MPVolume :: mouseMoveEvent(QMouseEvent *event)
{
	if (moving) {
		curr_delta = ((y_pos - event->y()) * (max - min + 1)) / 128;

		if ((curr_delta + curr_pos) > max)
			curr_delta = max - curr_pos;
		else if ((curr_delta + curr_pos) < min)
			curr_delta = min - curr_pos;

		emit valueChanged(curr_pos + curr_delta);

		update();
	}
}

void
MPVolume :: setRange(int from, int to, int middle)
{
	curr_pos = from;
	min = from;
	max = to;
	mid = middle;
}

int
MPVolume :: value(void) const
{
	return (curr_pos + curr_delta);
}

void
MPVolume :: setValue(int value)
{
	if (value > max)
		curr_pos = max;
	else if (value < min)
		curr_pos = min;
	else
		curr_pos = value;

	curr_delta = 0;

	emit valueChanged(curr_pos);

	update();
}

void
MPVolume :: enterEvent(QEvent *event)
{
	focus = 1;
	update();
}

void
MPVolume :: leaveEvent(QEvent *event)
{
	focus = 0;
	update();
}

void
MPVolume :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	QFont fnt;
	char buffer[16];
	const int m = 4;
	int w = width();
	int h = height();
	int val = value() - min;
	int angle = (val * 270 * 16) / (max - min + 1);
	int color = (val * (255 - 128)) / (max - min + 1);

	paint.setRenderHints(QPainter::Antialiasing, 1);

	QColor black(0,0,0);
	QColor button(128,128,128);
	QColor button_focus(128,192,128);
	QColor active(128+color,128,128);
	QColor background(0,0,0,0);

	fnt.setPixelSize(3*m);

	snprintf(buffer, sizeof(buffer), "-%d+", value());

	QString descr(buffer);

	paint.fillRect(QRectF(0,0,w,h), background);

	paint.setPen(QPen(black, 1));
	paint.setBrush(active);
	paint.drawPie(QRectF(m,m,w-(2*m),h-(2*m)),(180+45)*16, -angle);

	if (focus)
		paint.setBrush(button_focus);
	else
		paint.setBrush(button);

	paint.drawEllipse(QRectF((w/4)+m,(h/4)+m,(w/2)-(2*m), (h/2)-(2*m)));

	paint.setFont(fnt);

	QRectF sz = paint.boundingRect(QRectF(0,0,0,0), descr);

	paint.drawText(QPointF((w - sz.width()) / 2.0, h), descr);
}

Q_DECL_EXPORT int
main(int argc, char **argv)
{
	/* must be first, before any threads are created */
	signal(SIGPIPE, SIG_IGN);

	QApplication app(argc, argv);

	new MPMainWindow();
	
	return (app.exec());
}
