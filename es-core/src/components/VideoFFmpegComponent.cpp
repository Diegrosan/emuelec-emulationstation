#include "components/VideoFFmpegComponent.h"
#include "renderers/Renderer.h"
#include "resources/TextureResource.h"
#include "utils/StringUtil.h"
#include "PowerSaver.h"
#include "Settings.h"
#include "VolumeControl.h"
#include <SDL_timer.h>
#include <SDL.h>
#include <cmath>
#include "Log.h"

#define MATHPI 3.141592653589793238462643383279502884L

void VideoFFmpegComponent::init()
{
	// Ensure SDL audio subsystem is initialized before any video attempts to open audio device
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		LOG(LogError) << "[VideoFFmpeg] SDL_InitSubSystem(SDL_INIT_AUDIO) failed! Error: " << SDL_GetError();
	}

	// Map FFmpeg log level to EmulationStation log level
	if (Log::getReportingLevel() >= LogDebug)
		av_log_set_level(AV_LOG_WARNING);
	else
		av_log_set_level(AV_LOG_FATAL);

	avformat_network_init();
	
	LOG(LogInfo) << "[VideoFFmpeg] Backend: FFmpeg";
	LOG(LogInfo) << "[VideoFFmpeg] libavutil: " << AV_VERSION_MAJOR(avutil_version()) << "." << AV_VERSION_MINOR(avutil_version()) << "." << AV_VERSION_MICRO(avutil_version()) << " (Compile: " << LIBAVUTIL_VERSION_MAJOR << ")";
	LOG(LogInfo) << "[VideoFFmpeg] libavcodec: " << AV_VERSION_MAJOR(avcodec_version()) << "." << AV_VERSION_MINOR(avcodec_version()) << "." << AV_VERSION_MICRO(avcodec_version()) << " (Compile: " << LIBAVCODEC_VERSION_MAJOR << ")";
	LOG(LogInfo) << "[VideoFFmpeg] libavformat: " << AV_VERSION_MAJOR(avformat_version()) << "." << AV_VERSION_MINOR(avformat_version()) << "." << AV_VERSION_MICRO(avformat_version()) << " (Compile: " << LIBAVFORMAT_VERSION_MAJOR << ")";
	LOG(LogInfo) << "[VideoFFmpeg] libswscale: " << AV_VERSION_MAJOR(swscale_version()) << "." << AV_VERSION_MINOR(swscale_version()) << "." << AV_VERSION_MICRO(swscale_version()) << " (Compile: " << LIBSWSCALE_VERSION_MAJOR << ")";
	LOG(LogInfo) << "[VideoFFmpeg] version-info: " << av_version_info();
	LOG(LogInfo) << "[VideoFFmpeg] avutil_configuration: " << avutil_configuration();
}

VideoFFmpegComponent::VideoFFmpegComponent(Window* window) : VideoComponent(window),
	mFormatContext(nullptr), mCodecContext(nullptr), mVideoStreamIndex(-1), mSwsContext(nullptr),
	mAudioStreamIndex(-1), mAudioCodecContext(nullptr), mSwrContext(nullptr), mAudioDeviceID(0),
	mStopDecoding(false), mIsPaused(false), mTopLeftCrop(0.0f, 0.0f), mBottomRightCrop(1.0f, 1.0f)
{
	mSaturation = 1.0f;
	mElapsed = 0;
	mColorShift = 0xFFFFFFFF;
	mLinearSmooth = false;
	mLoops = -1;
	mCurrentLoop = 0;
	mTexture = nullptr;
	mEffect = VideoFFmpegFlags::VideoFFmpegEffect::BUMP;
	mContext.component = this;
}

VideoFFmpegComponent::~VideoFFmpegComponent()
{
	stopVideo();
}

void VideoFFmpegComponent::setupContext()
{
	mContext.valid = true;
	if (mContext.surfaces[0] == nullptr && mVideoWidth > 0 && mVideoHeight > 0)
	{
		mContext.surfaces[0] = new unsigned char[mVideoWidth * mVideoHeight * 4];
		mContext.surfaces[1] = new unsigned char[mVideoWidth * mVideoHeight * 4];
	}
}

void VideoFFmpegComponent::freeContext()
{
	mContext.valid = false;
	mContext.mutexes[0].lock();
	if (mContext.surfaces[0]) {
		delete[] mContext.surfaces[0];
		mContext.surfaces[0] = nullptr;
	}
	mContext.mutexes[0].unlock();

	mContext.mutexes[1].lock();
	if (mContext.surfaces[1]) {
		delete[] mContext.surfaces[1];
		mContext.surfaces[1] = nullptr;
	}
	mContext.mutexes[1].unlock();

	mContext.hasFrame[0] = false;
	mContext.hasFrame[1] = false;
}

void VideoFFmpegComponent::startVideo()
{
	if (!mIsPlaying)
	{
		mVideoWidth = 0;
		mVideoHeight = 0;

		mFormatContext = avformat_alloc_context();
		LOG(LogInfo) << "[VideoFFmpeg] Factory: VideoComponent::create() called VideoFFmpegComponent";
		LOG(LogInfo) << "[VideoFFmpeg] Opening video file: " << mVideoPath;
		int open_res = avformat_open_input(&mFormatContext, mVideoPath.c_str(), nullptr, nullptr);
		if (open_res != 0) {
			char errbuf[256];
			av_strerror(open_res, errbuf, sizeof(errbuf));
			LOG(LogError) << "[VideoFFmpeg] OPEN = FAIL. Error: " << errbuf;
			return;
		}
		if (mFormatContext && mFormatContext->iformat) {
			LOG(LogInfo) << "[VideoFFmpeg] Container format: " << mFormatContext->iformat->name << " (" << mFormatContext->iformat->long_name << ")";
		}
		LOG(LogInfo) << "[VideoFFmpeg] OPEN = PASS (File: " << mVideoPath << ")";

		int stream_info_res = avformat_find_stream_info(mFormatContext, nullptr);
		if (stream_info_res < 0) {
			char errbuf[256];
			av_strerror(stream_info_res, errbuf, sizeof(errbuf));
			LOG(LogError) << "[VideoFFmpeg] STREAM INFO = FAIL. Error: " << errbuf;
			return;
		}
		LOG(LogInfo) << "[VideoFFmpeg] STREAM INFO = PASS (Streams: " << mFormatContext->nb_streams << ")";

		mVideoStreamIndex = -1;
		const AVCodec* codec = nullptr;
		AVCodecParameters* codecParams = nullptr;

		for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
			AVStream* stream = mFormatContext->streams[i];
			AVCodecParameters* localParams = stream->codecpar;
			if (localParams->codec_type == AVMEDIA_TYPE_VIDEO) {
				mVideoStreamIndex = i;
				codecParams = localParams;
				codec = avcodec_find_decoder(localParams->codec_id);
				LOG(LogInfo) << "[VideoFFmpeg] VIDEO STREAM = PASS (Index: " << i << ", Codec ID: " << localParams->codec_id << ", WxH: " << localParams->width << "x" << localParams->height << ", format: " << localParams->format << ")";
				break;
			}
		}

		if (mVideoStreamIndex == -1 || !codec) {
			LOG(LogError) << "[VideoFFmpeg] VIDEO STREAM = FAIL or DECODER = FAIL";
			return;
		}
		LOG(LogInfo) << "[VideoFFmpeg] DECODER = PASS (Codec Name: " << (codec->name ? codec->name : "unknown") << ")";

		mCodecContext = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(mCodecContext, codecParams);

		int codec_open_res = avcodec_open2(mCodecContext, codec, nullptr);
		if (codec_open_res < 0) {
			char errbuf[256];
			av_strerror(codec_open_res, errbuf, sizeof(errbuf));
			LOG(LogError) << "[VideoFFmpeg] CODEC OPEN = FAIL. Error: " << errbuf;
			return;
		}
		LOG(LogInfo) << "[VideoFFmpeg] CODEC OPEN = PASS";

		// AUDIO SETUP
		mAudioStreamIndex = -1;
		const AVCodec* audioCodec = nullptr;
		AVCodecParameters* audioCodecParams = nullptr;

		for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
			AVStream* stream = mFormatContext->streams[i];
			AVCodecParameters* localParams = stream->codecpar;
			if (localParams->codec_type == AVMEDIA_TYPE_AUDIO) {
				mAudioStreamIndex = i;
				audioCodecParams = localParams;
				audioCodec = avcodec_find_decoder(localParams->codec_id);
				LOG(LogInfo) << "[VideoFFmpeg] AUDIO STREAM = PASS (Index: " << i << ", Codec ID: " << localParams->codec_id << ", Rate: " << localParams->sample_rate << ", Channels: " << localParams->ch_layout.nb_channels << ")";
				break;
			}
		}

		if (mAudioStreamIndex != -1 && audioCodec) {
			mAudioCodecContext = avcodec_alloc_context3(audioCodec);
			avcodec_parameters_to_context(mAudioCodecContext, audioCodecParams);
			
			if (avcodec_open2(mAudioCodecContext, audioCodec, nullptr) >= 0) {
				LOG(LogInfo) << "[VideoFFmpeg] AUDIO DECODER OPEN = PASS";
				
				SDL_AudioSpec desiredSpec, obtainedSpec;
				SDL_zero(desiredSpec);
				desiredSpec.freq = 44100;
				desiredSpec.format = AUDIO_S16SYS;
				desiredSpec.channels = 2;
				desiredSpec.samples = 2048;
				
				mAudioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
				if (mAudioDeviceID != 0) {
					LOG(LogInfo) << "[VideoFFmpeg] AUDIO DEVICE OPEN = PASS (Device ID: " << mAudioDeviceID << ", freq: " << obtainedSpec.freq << ", channels: " << obtainedSpec.channels << ")";
					
					AVChannelLayout outLayout;
					av_channel_layout_default(&outLayout, obtainedSpec.channels);
					
					int ret = swr_alloc_set_opts2(&mSwrContext,
						&outLayout, AV_SAMPLE_FMT_S16, obtainedSpec.freq,
						&mAudioCodecContext->ch_layout, mAudioCodecContext->sample_fmt, mAudioCodecContext->sample_rate,
						0, nullptr);
						
					if (ret >= 0 && swr_init(mSwrContext) >= 0) {
						LOG(LogInfo) << "[VideoFFmpeg] SWR INIT = PASS";
						SDL_PauseAudioDevice(mAudioDeviceID, 0); // start playing immediately (queue is empty)
					} else {
						LOG(LogError) << "[VideoFFmpeg] SWR INIT = FAIL";
						if (mSwrContext) swr_free(&mSwrContext);
						SDL_CloseAudioDevice(mAudioDeviceID);
						mAudioDeviceID = 0;
					}
				} else {
					LOG(LogError) << "[VideoFFmpeg] AUDIO DEVICE OPEN = FAIL. Error: " << SDL_GetError();
				}
			} else {
				LOG(LogError) << "[VideoFFmpeg] AUDIO DECODER OPEN = FAIL";
			}
		} else {
			LOG(LogInfo) << "[VideoFFmpeg] AUDIO STREAM = NOT FOUND";
		}

		mVideoWidth = mCodecContext->width;
		mVideoHeight = mCodecContext->height;

		setupContext();
		resize();

		mIsPlaying = true;
		mIsPaused = false;
		mStopDecoding = false;
		mDecodeThread = std::thread(&VideoFFmpegComponent::decodeThread, this);
		mPlayingVideoPath = mVideoPath;
	}
}

void VideoFFmpegComponent::decodeThread()
{
	LOG(LogInfo) << "[VideoFFmpeg] decodeThread started";
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	AVFrame* frameRGBA = av_frame_alloc();

	int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, mCodecContext->width, mCodecContext->height, 1);
	uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
	LOG(LogInfo) << "[VideoFFmpeg] BUFFER ALLOCATED: " << numBytes << " bytes for " << mCodecContext->width << "x" << mCodecContext->height;

	av_image_fill_arrays(frameRGBA->data, frameRGBA->linesize, buffer, AV_PIX_FMT_RGBA, mCodecContext->width, mCodecContext->height, 1);

	mSwsContext = sws_getContext(mCodecContext->width, mCodecContext->height, mCodecContext->pix_fmt,
								 mCodecContext->width, mCodecContext->height, AV_PIX_FMT_RGBA,
								 SWS_BILINEAR, nullptr, nullptr, nullptr);
	
	if (!mSwsContext) {
		LOG(LogError) << "[VideoFFmpeg] SWSCALE = FAIL (source fmt: " << mCodecContext->pix_fmt << ", dst fmt: " << AV_PIX_FMT_RGBA << ")";
	} else {
		LOG(LogInfo) << "[VideoFFmpeg] SWSCALE = PASS";
	}

	int packetCount = 0;
	int frameCount = 0;

	AVStream* stream = mFormatContext->streams[mVideoStreamIndex];
	double timeBase = av_q2d(stream->time_base);
	double startTime = SDL_GetTicks() / 1000.0;
	double ptsOffset = -1.0;
	double currentVideoTime = 0.0;

	while (!mStopDecoding)
	{
		if (mIsPaused) {
			SDL_Delay(10);
			startTime = (SDL_GetTicks() / 1000.0) - currentVideoTime;
			continue;
		}

		if (av_read_frame(mFormatContext, packet) >= 0) {
			if (packet->stream_index == mVideoStreamIndex) {
				packetCount++;
				if (packetCount <= 10 || packetCount % 100 == 0) {
					LOG(LogInfo) << "[VideoFFmpeg] PACKETS = PASS (Count: " << packetCount << ", size: " << packet->size << ", pts: " << packet->pts << ")";
				}

				int send_res = avcodec_send_packet(mCodecContext, packet);
				if (send_res < 0 && send_res != AVERROR(EAGAIN) && send_res != AVERROR_EOF) {
					char errbuf[256];
					av_strerror(send_res, errbuf, sizeof(errbuf));
					LOG(LogError) << "[VideoFFmpeg] avcodec_send_packet FAIL: " << errbuf;
				} else if (send_res == 0) {
					while (avcodec_receive_frame(mCodecContext, frame) == 0) {
						if (mStopDecoding) break;

						frameCount++;
						if (frameCount <= 10 || frameCount % 100 == 0) {
							LOG(LogInfo) << "[VideoFFmpeg] FRAMES = PASS (Count: " << frameCount << ", WxH: " << frame->width << "x" << frame->height << ", fmt: " << frame->format << ")";
						}

						if (frame->width <= 0 || frame->height <= 0 || frame->format < 0 || frame->data[0] == nullptr) {
							LOG(LogError) << "[VideoFFmpeg] FRAME VALIDATION = FAIL";
						}

						double pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : frame->best_effort_timestamp * timeBase;
						if (ptsOffset < 0.0) ptsOffset = pts;

						currentVideoTime = pts - ptsOffset;
						double currentRealTime = (SDL_GetTicks() / 1000.0) - startTime;

						if (currentRealTime < currentVideoTime) {
							SDL_Delay((currentVideoTime - currentRealTime) * 1000.0);
						}

						if (mSwsContext) {
							sws_scale(mSwsContext, (uint8_t const* const*)frame->data, frame->linesize, 0, mCodecContext->height, frameRGBA->data, frameRGBA->linesize);
						}

						int targetBuffer = mContext.surfaceId ^ 1;
						mContext.mutexes[targetBuffer].lock();
						mContext.hasFrame[targetBuffer] = false;
						if (mContext.surfaces[targetBuffer]) {
							memcpy(mContext.surfaces[targetBuffer], frameRGBA->data[0], numBytes);
						}
						mContext.hasFrame[targetBuffer] = true;
						mContext.surfaceId = targetBuffer;
						mContext.mutexes[targetBuffer].unlock();
						
						if (!mIsWaitingForVideoToStart && mIsPlaying) {
						    // Trigger frame ready for component logic if necessary
						}
					}
				}
			}
			else if (mAudioStreamIndex != -1 && packet->stream_index == mAudioStreamIndex) {
				bool shouldPlayAudio = mPlayAudio && ((!mScreensaverMode && Settings::getInstance()->getBool("VideoAudio")) || (mScreensaverMode && !Settings::getInstance()->getBool("ScreenSaverVideoMute")));
				if (shouldPlayAudio && mAudioDeviceID != 0 && mAudioCodecContext && mSwrContext && !mIsPaused) {
					int send_res = avcodec_send_packet(mAudioCodecContext, packet);
					if (send_res == 0) {
						while (avcodec_receive_frame(mAudioCodecContext, frame) == 0) {
							if (mStopDecoding) break;
							
							uint8_t* output_buffer = nullptr;
							int out_samples = av_rescale_rnd(swr_get_delay(mSwrContext, mAudioCodecContext->sample_rate) + frame->nb_samples, 44100, mAudioCodecContext->sample_rate, AV_ROUND_UP);
							av_samples_alloc(&output_buffer, nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 0);
							
							int ret = swr_convert(mSwrContext, &output_buffer, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
							if (ret > 0) {
								int out_size = av_samples_get_buffer_size(nullptr, 2, ret, AV_SAMPLE_FMT_S16, 1);
								int volume = VolumeControl::getInstance()->getVolume();
								if (volume > 0) {
									if (volume < 100) {
										int sdlVolume = (volume * SDL_MIX_MAXVOLUME) / 100;
										int16_t* samples = (int16_t*)output_buffer;
										int num_samples = out_size / sizeof(int16_t);
										for (int i = 0; i < num_samples; i++) {
											samples[i] = (int16_t)((samples[i] * sdlVolume) / SDL_MIX_MAXVOLUME);
										}
									}
									SDL_QueueAudio(mAudioDeviceID, output_buffer, out_size);
								}
							}
							
							av_freep(&output_buffer);
						}
					}
				}
			}
			av_packet_unref(packet);
		} else {
			LOG(LogInfo) << "[VideoFFmpeg] EOF REACHED. Decoded Frames: " << frameCount;
			if (mLoops != 0) {
				LOG(LogInfo) << "[VideoFFmpeg] LOOP ENABLED. Seeking to 0...";
				av_seek_frame(mFormatContext, mVideoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
				avcodec_flush_buffers(mCodecContext);
				if (mAudioStreamIndex != -1 && mAudioCodecContext) {
					avcodec_flush_buffers(mAudioCodecContext);
					if (mAudioDeviceID != 0) {
						SDL_ClearQueuedAudio(mAudioDeviceID);
					}
				}
				startTime = SDL_GetTicks() / 1000.0;
				ptsOffset = -1.0;
				currentVideoTime = 0.0;
			} else {
				LOG(LogInfo) << "[VideoFFmpeg] LOOP DISABLED. Stopping.";
				break;
			}
		}
	}
	LOG(LogInfo) << "[VideoFFmpeg] decodeThread exiting (Frames decoded: " << frameCount << ")";

	av_free(buffer);
	av_frame_free(&frameRGBA);
	av_frame_free(&frame);
	av_packet_free(&packet);
	sws_freeContext(mSwsContext);
	mSwsContext = nullptr;
	if (mSwrContext) {
		swr_free(&mSwrContext);
		mSwrContext = nullptr;
	}
}

void VideoFFmpegComponent::stopVideo()
{
	mIsPlaying = false;
	mIsPaused = false;
	mStopDecoding = true;
	if (mDecodeThread.joinable()) {
		mDecodeThread.join();
	}
	
	if (mCodecContext) {
		avcodec_free_context(&mCodecContext);
		mCodecContext = nullptr;
	}
	if (mAudioCodecContext) {
		avcodec_free_context(&mAudioCodecContext);
		mAudioCodecContext = nullptr;
	}
	if (mFormatContext) {
		avformat_close_input(&mFormatContext);
		mFormatContext = nullptr;
	}
	if (mAudioDeviceID != 0) {
		SDL_CloseAudioDevice(mAudioDeviceID);
		mAudioDeviceID = 0;
	}
	if (mSwrContext) {
		swr_free(&mSwrContext);
		mSwrContext = nullptr;
	}
	freeContext();
	mTexture = nullptr;
	mPlayingVideoPath = "";
}
void VideoFFmpegComponent::pauseVideo() 
{ 
	mIsPaused = true; 
	if (mAudioDeviceID != 0) {
		SDL_PauseAudioDevice(mAudioDeviceID, 1);
	}
}

void VideoFFmpegComponent::resumeVideo() 
{ 
	mIsPaused = false; 
	if (mAudioDeviceID != 0) {
		SDL_PauseAudioDevice(mAudioDeviceID, 0);
	}
}
void VideoFFmpegComponent::handleLooping() { }
bool VideoFFmpegComponent::isPaused() { return mIsPaused; }

void VideoFFmpegComponent::onVideoStarted()
{
	VideoComponent::onVideoStarted();
	resize();
}

void VideoFFmpegComponent::update(int deltaTime)
{
	VideoComponent::update(deltaTime);
	mElapsed += deltaTime;
	if (mTexture != nullptr) {
		// Nothing to update periodically besides standard logic
	}
}

void VideoFFmpegComponent::render(const Transform4x4f& parentTrans)
{
	if (!isVisible())
		return;

	VideoComponent::render(parentTrans);

	Transform4x4f trans = parentTrans * getTransform();

	if (mIsPlaying)
	{
		static int renderCount = 0;
		renderCount++;
		if (renderCount <= 10 || renderCount % 100 == 0) {
			LOG(LogInfo) << "[VideoFFmpeg] RENDER called. Frame count: " << renderCount;
		}

		int frame = mContext.surfaceId;
		if (mContext.hasFrame[frame]) {
			mContext.mutexes[frame].lock();
			if (mTexture == nullptr) {
				mTexture = TextureResource::get("", false, mLinearSmooth);
				if (mTexture) {
					LOG(LogInfo) << "[VideoFFmpeg] TEXTURE CREATED";
				} else {
					LOG(LogError) << "[VideoFFmpeg] TEXTURE = FAIL";
				}
			}
			if (mTexture != nullptr && mContext.surfaces[frame] != nullptr) {
				mTexture->updateFromExternalPixels(mContext.surfaces[frame], mVideoWidth, mVideoHeight);
				if (renderCount <= 10 || renderCount % 100 == 0) {
					LOG(LogInfo) << "[VideoFFmpeg] TEXTURE UPDATE (size: " << mVideoWidth << "x" << mVideoHeight << ")";
				}
			}
			mContext.mutexes[frame].unlock();
		}

		if (mTexture != nullptr)
		{
			Renderer::setMatrix(trans);
			
			float opacity = getOpacity() / 255.0f;
			if (isFading()) {
				// Don't modify mFadeIn here, it is handled by VideoComponent::update
				opacity *= mFadeIn;
			}
			unsigned int color = Renderer::convertColor((mColorShift & 0xFFFFFF00) | (unsigned char)((mColorShift & 0xFF) * opacity));
			mVertices[0].col = color;
			mVertices[1].col = color;
			mVertices[2].col = color;
			mVertices[3].col = color;
			
			if (mTexture) {
				bool bindResult = mTexture->bind();
				if (renderCount <= 10 || renderCount % 100 == 0) {
					LOG(LogInfo) << "[VideoFFmpeg] BIND TEXTURE result: " << bindResult << " mTextureID: " << (mTexture ? "exists" : "null");
					LOG(LogInfo) << "[VideoFFmpeg] VERTICES: "
								 << "v0(" << mVertices[0].pos.x() << "," << mVertices[0].pos.y() << "), "
								 << "v1(" << mVertices[1].pos.x() << "," << mVertices[1].pos.y() << "), "
								 << "v2(" << mVertices[2].pos.x() << "," << mVertices[2].pos.y() << "), "
								 << "v3(" << mVertices[3].pos.x() << "," << mVertices[3].pos.y() << "), "
								 << "mSize(" << mSize.x() << "," << mSize.y() << "), "
								 << "mVideo(" << mVideoWidth << "x" << mVideoHeight << ")";
					if (mColorShift != 0xFFFFFFFF) {
						LOG(LogInfo) << "[VideoFFmpeg] WARNING: Color shift is " << std::hex << mColorShift;
					}
				}
				if (bindResult) {
					Renderer::drawTriangleStrips(&mVertices[0], 4);
					Renderer::bindTexture(0);
				}
			}
		}
	}
}

void VideoFFmpegComponent::setResize(float width, float height)
{
	mTargetSize = Vector2f(width, height);
	mSize = mTargetSize;
	mTargetIsMax = false;
	mTargetIsMin = false;
	mStaticImage.setMaxSize(width, height);
	resize();
}
void VideoFFmpegComponent::setMaxSize(float width, float height)
{
	mTargetSize = Vector2f(width, height);
	mSize = mTargetSize;
	mTargetIsMax = true;
	mTargetIsMin = false;
	mStaticImage.setMaxSize(width, height);
	resize();
}
void VideoFFmpegComponent::setMinSize(float width, float height)
{
	mTargetSize = Vector2f(width, height);
	mSize = mTargetSize;
	mTargetIsMax = false;
	mTargetIsMin = true;
	mStaticImage.setMaxSize(width, height);
	resize();
}

void VideoFFmpegComponent::resize()
{
	
	Vector2f textureSize((float)mVideoWidth, (float)mVideoHeight);
	if (textureSize.x() == 0 || textureSize.y() == 0) return;

	if (mTargetIsMax) {
		mSize = textureSize;
		float scaleX = mTargetSize.x() / mSize.x();
		float scaleY = mTargetSize.y() / mSize.y();
		float scale = Math::min(scaleX, scaleY);
		mSize *= scale;
	} else if (mTargetIsMin) {
		mSize = textureSize;
		float scaleX = mTargetSize.x() / mSize.x();
		float scaleY = mTargetSize.y() / mSize.y();
		float scale = Math::max(scaleX, scaleY);
		mSize *= scale;
	}
	updateVertices();
}

void VideoFFmpegComponent::updateVertices()
{
	mVertices[0] = { { 0.0f, 0.0f }, { mTopLeftCrop.x(), mTopLeftCrop.y() }, mColorShift };
	mVertices[1] = { { 0.0f, mSize.y() }, { mTopLeftCrop.x(), mBottomRightCrop.y() }, mColorShift };
	mVertices[2] = { { mSize.x(), 0.0f }, { mBottomRightCrop.x(), mTopLeftCrop.y() }, mColorShift };
	mVertices[3] = { { mSize.x(), mSize.y() }, { mBottomRightCrop.x(), mBottomRightCrop.y() }, mColorShift };
}

void VideoFFmpegComponent::applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties) {
	VideoComponent::applyTheme(theme, view, element, properties);
}

void VideoFFmpegComponent::setColorShift(unsigned int color) {
	mColorShift = color;
	updateColors();
}

void VideoFFmpegComponent::updateColors() {
	mVertices[0].col = mColorShift;
	mVertices[1].col = mColorShift;
	mVertices[2].col = mColorShift;
	mVertices[3].col = mColorShift;
}

void VideoFFmpegComponent::onShow() {
	VideoComponent::onShow();
}

ThemeData::ThemeElement::Property VideoFFmpegComponent::getProperty(const std::string name) {
	return VideoComponent::getProperty(name);
}

void VideoFFmpegComponent::setProperty(const std::string name, const ThemeData::ThemeElement::Property& value) {
	VideoComponent::setProperty(name, value);
}

void VideoFFmpegComponent::setSaturation(float saturation) {
	mSaturation = saturation;
}

void VideoFFmpegComponent::setRoundCorners(float value) {
	mRoundCorners = value;
	updateRoundCorners();
}

void VideoFFmpegComponent::onSizeChanged() {
	VideoComponent::onSizeChanged();
	updateVertices();
}

void VideoFFmpegComponent::onPaddingChanged() {
	VideoComponent::onPaddingChanged();
}

Vector2f VideoFFmpegComponent::getSize() const {
	return GuiComponent::getSize() * (mBottomRightCrop - mTopLeftCrop);
}

void VideoFFmpegComponent::updateRoundCorners() { }
void VideoFFmpegComponent::crop(float left, float top, float right, float bot) {
	mTopLeftCrop.x() = left;
	mTopLeftCrop.y() = top;
	mBottomRightCrop.x() = right;
	mBottomRightCrop.y() = bot;
	updateVertices();
}
