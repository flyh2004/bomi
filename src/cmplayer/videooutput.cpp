#include "videooutput.hpp"
#include "videoframe.hpp"
#include "mpcore.hpp"
#include "hwaccel.hpp"
#include "videorendereritem.hpp"
#include "playengine.hpp"

extern "C" {
#include <libvo/video_out.h>
#include <libmpcodecs/img_format.h>
#include <libmpcodecs/vfcap.h>
#include <libmpcodecs/mp_image.h>
#include <libvo/fastmemcpy.h>
#include <input/input.h>
#include <libvo/osd.h>
#include <sub/sub.h>
#include <libmpdemux/stheader.h>
}

struct VideoOutput::Data {
	vo_driver driver;
	vo_info_t info;
	VideoFormat format;
	HwAccel *hwaccel = nullptr;
	bool hwaccelActivated = false;
	void deleteHwAccel() {
//		renderer->makeCurrent();
//		glBindTexture(GL_TEXTURE_2D, renderer->texture(0));
		delete hwaccel;
//		renderer->doneCurrent();
		hwaccel = nullptr;
	}
	QMutex mutex;
	QWaitCondition cond;
	mp_image_t *mpimg = nullptr;
	VideoFrame *frame = nullptr;
	PlayEngine *engine = nullptr;
};

VideoOutput::VideoOutput(PlayEngine *engine): d(new Data) {
	memset(&d->info, 0, sizeof(d->info));
	memset(&d->driver, 0, sizeof(d->driver));

	d->info.name = "CMPlayer video output";
	d->info.short_name = "cmp";
	d->info.author = "xylosper <darklin20@gmail.com>";
	d->info.comment = "";

	d->driver.is_new = true,
	d->driver.info = &d->info;
	d->driver.preinit = preinit;
	d->driver.config = config;
	d->driver.control = control;
	d->driver.draw_slice = drawSlice;
	d->driver.draw_osd = drawOsd;
	d->driver.flip_page = flipPage;
	d->driver.check_events = checkEvents;
	d->driver.uninit = uninit;

	d->engine = engine;
}

VideoOutput::~VideoOutput() {
	d->deleteHwAccel();
	delete d;
}

struct vo *VideoOutput::vo_create(MPContext *mpctx) {
	struct vo *vo = reinterpret_cast<struct vo*>(talloc_ptrtype(NULL, vo));
	memset(vo, 0, sizeof(*vo));
	vo->opts = &mpctx->opts;
	vo->key_fifo = mpctx->key_fifo;
	vo->input_ctx = mpctx->input;
	vo->event_fd = -1;
	vo->registered_fd = -1;
	vo->priv = this;
	vo->driver = &d->driver;
	if (!vo->driver->preinit(vo, NULL))
		return vo;
	talloc_free_children(vo);
	talloc_free(vo);
	return nullptr;
}

const VideoFormat &VideoOutput::format() const {
	return d->format;
}

bool VideoOutput::usingHwAccel() const {
	return d->hwaccelActivated;
}

int VideoOutput::config(struct vo *vo, uint32_t w_s, uint32_t h_s, uint32_t, uint32_t, uint32_t, uint32_t fmt) {
	auto self = reinterpret_cast<VideoOutput*>(vo->priv);
	Data *d = self->d;
	d->hwaccelActivated = false;
#ifdef Q_OS_X11
	auto avctx = HwAccelInfo::get().avctx();
	if (fmt == IMGFMT_VDPAU && avctx) {
		if (d->hwaccel) {
			if (!d->hwaccel->isCompatibleWith(avctx))
				d->deleteHwAccel();
		}
		if (!d->hwaccel)
			d->hwaccel = new HwAccel(avctx);
		if (!d->hwaccel->isUsable()) {
			d->deleteHwAccel();
			return -1;
		}
		avctx->hwaccel_context = d->hwaccel->context();
		d->format = d->hwaccel->format();
		d->format.stride = avctx->width*4;
		d->format.width_stride = avctx->width;
		d->format.height = avctx->height;
		d->hwaccelActivated = true;
	} else
#endif
	d->format = VideoFormat(imgfmtToVideoFormatType(fmt));
#ifdef Q_OS_X11
	if (d->hwaccelActivated && d->hwaccel && d->hwaccel->isUsable()) {
		d->renderer->makeCurrent();
		d->hwaccel->createSurface(d->renderer->textures());
		d->renderer->doneCurrent();
	}
#endif
	qDebug() << "vo configured";
	return 0;
}

bool VideoOutput::getImage(void *data) {
#ifdef Q_OS_MAC
	Q_UNUSED(data);
	return false;
#endif
#ifdef Q_OS_X11
	auto mpi = reinterpret_cast<mp_image_t*>(data);
	return d->hwaccel && d->hwaccel->isUsable() && d->hwaccel->setBuffer(mpi);
#endif
}

void VideoOutput::drawImage(void *data) {
	mp_image_t *mpi = reinterpret_cast<mp_image_t*>(data);
	if (auto renderer = d->engine->videoRenderer()) {
		VideoFrame &frame = renderer->getNextFrame();
		frame.setFormat(d->format);
		if (frame.copy(mpi))
			emit formatChanged(d->format = frame.format());
	}
}

int VideoOutput::control(struct vo *vo, uint32_t req, void *data) {
	VideoOutput *v = reinterpret_cast<VideoOutput*>(vo->priv);
	switch (req) {
	case VOCTRL_QUERY_FORMAT:
		return v->queryFormat(*reinterpret_cast<uint32_t*>(data));
	case VOCTRL_DRAW_IMAGE:
		v->drawImage(data);
		return VO_TRUE;
	case VOCTRL_REDRAW_FRAME:
		if (auto renderer = v->d->engine->videoRenderer())
			renderer->update();
		return VO_TRUE;
	case VOCTRL_FULLSCREEN:
		return VO_TRUE;
	case VOCTRL_UPDATE_SCREENINFO:
//		update_xinerama_info(vo);
		return VO_TRUE;
	case VOCTRL_GET_IMAGE:
		return v->getImage(data);
	case VOCTRL_PAUSE:
	case VOCTRL_RESUME:
	case VOCTRL_GET_PANSCAN:
	case VOCTRL_SET_PANSCAN:
	case VOCTRL_SET_EQUALIZER:
	case VOCTRL_GET_EQUALIZER:
	case VOCTRL_SET_YUV_COLORSPACE:;
	case VOCTRL_GET_YUV_COLORSPACE:;
	case VOCTRL_ONTOP:
	default:
		return VO_NOTIMPL;
	}
}

int VideoOutput::drawSlice(struct vo */*vo*/, uint8_t */*src*/[], int /*stride*/[], int w, int h, int x, int y) {
	qDebug() << "drawSlice();" << x << y << w << h;
	return true;
}

void VideoOutput::drawOsd(struct vo *vo, struct osd_state *osd) {
	Data *d = reinterpret_cast<VideoOutput*>(vo->priv)->d;
//	osd_draw_text(osd, d->format.width, d->format.height, VideoRenderer::drawAlpha, d->renderer);
}

void VideoOutput::flipPage(struct vo *vo) {
	Data *d = reinterpret_cast<VideoOutput*>(vo->priv)->d;
	if (auto renderer = d->engine->videoRenderer())
		renderer->next();
}

void VideoOutput::checkEvents(struct vo */*vo*/) {}

int VideoOutput::queryFormat(int format) {
	switch (format) {
	case IMGFMT_I420:
	case IMGFMT_YV12:
	case IMGFMT_NV12:
	case IMGFMT_YUY2:
	case IMGFMT_UYVY:
#ifdef Q_OS_X11
	case IMGFMT_VDPAU:
#endif
		return VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW
			| VFCAP_HWSCALE_UP | VFCAP_HWSCALE_DOWN | VFCAP_ACCEPT_STRIDE | VOCAP_NOSLICES;
	default:
		return 0;
	}
}

