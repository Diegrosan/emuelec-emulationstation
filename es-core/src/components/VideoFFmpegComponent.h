#pragma once
#ifndef ES_CORE_COMPONENTS_VIDEO_FFMPEG_COMPONENT_H
#define ES_CORE_COMPONENTS_VIDEO_FFMPEG_COMPONENT_H

#include "VideoComponent.h"
#include "ThemeData.h"
#include "renderers/Renderer.h"
#include <mutex>
#include <thread>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <SDL_audio.h>

struct FFmpegContext 
{
	FFmpegContext()
	{
		surfaces[0] = nullptr;
		surfaces[1] = nullptr;
		component = nullptr;
		valid = false;
		hasFrame[0] = false;
		hasFrame[1] = false;
		surfaceId = 0;
	}

	int					surfaceId;
	unsigned char*		surfaces[2];	
	std::mutex			mutexes[2];
	bool				hasFrame[2];

	VideoComponent*		component;
	bool				valid;	
};

namespace VideoFFmpegFlags
{
	enum VideoFFmpegEffect
	{
		NONE,
		BUMP,
		SIZE,
		SLIDERIGHT
	};
}

class VideoFFmpegComponent : public VideoComponent
{
public:
	static void init();

	VideoFFmpegComponent(Window* window);
	virtual ~VideoFFmpegComponent();

	void render(const Transform4x4f& parentTrans) override;

	void setResize(float width, float height);
	void setMaxSize(float width, float height);
	void setMinSize(float width, float height);

	virtual void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties);
	virtual void update(int deltaTime);

	void	setColorShift(unsigned int color);

	virtual void onShow() override;

	ThemeData::ThemeElement::Property getProperty(const std::string name) override;
	void setProperty(const std::string name, const ThemeData::ThemeElement::Property& value) override;

	void setEffect(VideoFFmpegFlags::VideoFFmpegEffect effect) { mEffect = effect; }

	bool getLinearSmooth() { return mLinearSmooth; }
	void setLinearSmooth(bool value = true) { mLinearSmooth = value; }

	void setSaturation(float saturation);

	void setRoundCorners(float value) override;
	void onSizeChanged() override;
	void onPaddingChanged() override;
	
	Vector2f getSize() const override;

private:
	void resize();
	virtual void startVideo();
	virtual void stopVideo();
	virtual void pauseVideo();
	virtual void resumeVideo();
	virtual bool isPaused();
	virtual void handleLooping();
	virtual void onVideoStarted();

	void setupContext();
	void freeContext();

	void decodeThread();

private:
	void crop(float left, float top, float right, float bot);

	AVFormatContext* mFormatContext;
	AVCodecContext* mCodecContext;
	int mVideoStreamIndex;
	SwsContext* mSwsContext;

	// Audio
	int mAudioStreamIndex;
	AVCodecContext* mAudioCodecContext;
	SwrContext* mSwrContext;
	SDL_AudioDeviceID mAudioDeviceID;

	std::thread mDecodeThread;
	std::atomic<bool> mStopDecoding;
	std::atomic<bool> mIsPaused;

	FFmpegContext mContext;

	std::string mSubtitlePath;
	std::string mSubtitleTmpFile;
	Renderer::ShaderInfo mCustomShader;

	VideoFFmpegFlags::VideoFFmpegEffect mEffect;

	unsigned int mColorShift;
	int mElapsed;

	int mCurrentLoop;
	int mLoops;

	bool mLinearSmooth;
	float mSaturation;

	void updateVertices();
	void updateColors();
	void updateRoundCorners();
	
	Renderer::Vertex mVertices[4];
	std::vector<Renderer::Vertex> mRoundCornerStencil;

	Vector2f mTopLeftCrop;
	Vector2f mBottomRightCrop;
};

#endif // ES_CORE_COMPONENTS_VIDEO_FFMPEG_COMPONENT_H
