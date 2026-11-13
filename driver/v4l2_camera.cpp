#include "driver/v4l2_camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

namespace prts::driver {
namespace {

constexpr int kBufferCount = 4;
// Upper bound on how many stale frames one grab may skip, so a misbehaving
// driver cannot keep the loop busy forever.
constexpr int kMaxDrain = 8;

int xioctl(int descriptor, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(descriptor, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string errorText(const std::string& what, int code) {
    return what + ": " + std::strerror(code);
}

}  // namespace

V4l2Camera::~V4l2Camera() { close(); }

bool V4l2Camera::configure(const model::CameraConfig& config, std::string& error) {
    v4l2_capability capability{};
    if (xioctl(device_, VIDIOC_QUERYCAP, &capability) < 0) {
        error = errorText("VIDIOC_QUERYCAP", errno);
        return false;
    }
    if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        error = "device does not support single-planar video capture";
        return false;
    }
    if ((capability.capabilities & V4L2_CAP_STREAMING) == 0) {
        error = "device does not support streaming I/O";
        return false;
    }

    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = static_cast<std::uint32_t>(config.width);
    format.fmt.pix.height = static_cast<std::uint32_t>(config.height);
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (config.pixelFormat == "YUYV") {
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    }
    if (xioctl(device_, VIDIOC_S_FMT, &format) < 0) {
        error = errorText("VIDIOC_S_FMT", errno);
        return false;
    }

    width_ = static_cast<int>(format.fmt.pix.width);
    height_ = static_cast<int>(format.fmt.pix.height);
    format_ = format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG ? "MJPG" : "YUYV";

    // Ask for the frame rate, the driver may clamp it.
    v4l2_streamparm parameters{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parameters.parm.capture.timeperframe.numerator = 1;
    parameters.parm.capture.timeperframe.denominator =
        static_cast<std::uint32_t>(config.fps > 0 ? config.fps : 10);
    xioctl(device_, VIDIOC_S_PARM, &parameters);

    v4l2_requestbuffers request{};
    request.count = kBufferCount;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(device_, VIDIOC_REQBUFS, &request) < 0) {
        error = errorText("VIDIOC_REQBUFS", errno);
        return false;
    }
    if (request.count < 1) {
        error = "no capture buffers were granted";
        return false;
    }

    buffers_.resize(request.count);
    for (std::size_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = static_cast<std::uint32_t>(index);
        if (xioctl(device_, VIDIOC_QUERYBUF, &buffer) < 0) {
            error = errorText("VIDIOC_QUERYBUF", errno);
            return false;
        }
        buffers_[index].length = buffer.length;
        buffers_[index].start = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                       device_, static_cast<off_t>(buffer.m.offset));
        if (buffers_[index].start == MAP_FAILED) {
            buffers_[index].start = nullptr;
            error = errorText("mmap", errno);
            return false;
        }
    }
    return true;
}

bool V4l2Camera::startStreaming(std::string& error) {
    for (std::size_t index = 0; index < buffers_.size(); ++index) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = static_cast<std::uint32_t>(index);
        if (xioctl(device_, VIDIOC_QBUF, &buffer) < 0) {
            error = errorText("VIDIOC_QBUF", errno);
            return false;
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(device_, VIDIOC_STREAMON, &type) < 0) {
        error = errorText("VIDIOC_STREAMON", errno);
        return false;
    }
    return true;
}

bool V4l2Camera::open(const model::CameraConfig& config, std::string& error) {
    close();
    device_ = ::open(config.device.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (device_ < 0) {
        error = errorText("open " + config.device, errno);
        return false;
    }
    if (!configure(config, error)) {
        close();
        return false;
    }
    if (!startStreaming(error)) {
        close();
        return false;
    }
    return true;
}

bool V4l2Camera::grab(model::CameraFrame& frame, int timeoutMs, std::string& error) {
    if (device_ < 0) {
        error = "camera is not open";
        return false;
    }

    pollfd descriptor{device_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, timeoutMs);
    if (ready < 0) {
        if (errno == EINTR) {
            error = "timeout";
            return false;
        }
        error = errorText("poll", errno);
        return false;
    }
    if (ready == 0) {
        error = "timeout";
        return false;
    }

    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (xioctl(device_, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) {
            error = "timeout";
            return false;
        }
        error = errorText("VIDIOC_DQBUF", errno);
        return false;
    }

    const Buffer& slot = buffers_[buffer.index];
    const std::size_t used = std::min<std::size_t>(buffer.bytesused, slot.length);
    frame.data.assign(static_cast<const std::uint8_t*>(slot.start),
                      static_cast<const std::uint8_t*>(slot.start) + used);
    if (xioctl(device_, VIDIOC_QBUF, &buffer) < 0) {
        error = errorText("VIDIOC_QBUF", errno);
        return false;
    }

    // The sensor keeps filling buffers while the publisher works, so the queue
    // can hold pictures older than the one just dequeued. Take the newest
    // available frame and drop the backlog: a live picture matters more than a
    // complete one.
    frame.drained = 0;
    for (int attempt = 0; attempt < kMaxDrain; ++attempt) {
        v4l2_buffer newer{};
        newer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        newer.memory = V4L2_MEMORY_MMAP;
        if (xioctl(device_, VIDIOC_DQBUF, &newer) < 0) {
            break;  // EAGAIN: nothing newer is waiting
        }
        const Buffer& newerSlot = buffers_[newer.index];
        const std::size_t newerUsed = std::min<std::size_t>(newer.bytesused, newerSlot.length);
        frame.data.assign(static_cast<const std::uint8_t*>(newerSlot.start),
                          static_cast<const std::uint8_t*>(newerSlot.start) + newerUsed);
        if (xioctl(device_, VIDIOC_QBUF, &newer) < 0) {
            error = errorText("VIDIOC_QBUF", errno);
            return false;
        }
        ++frame.drained;
    }

    frame.sequence = sequence_++;
    frame.timestampMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    frame.width = width_;
    frame.height = height_;
    frame.format = format_;
    return true;
}

void V4l2Camera::releaseBuffers() {
    for (Buffer& buffer : buffers_) {
        if (buffer.start != nullptr) {
            ::munmap(buffer.start, buffer.length);
            buffer.start = nullptr;
        }
        buffer.length = 0;
    }
    buffers_.clear();
}

void V4l2Camera::close() {
    if (device_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(device_, VIDIOC_STREAMOFF, &type);
        releaseBuffers();
        ::close(device_);
        device_ = -1;
    }
}

}  // namespace prts::driver
