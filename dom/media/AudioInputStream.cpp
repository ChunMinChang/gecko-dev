#include "AudioInputStream.h"

CubebStream::CubebStream(Listener* aListener): mListener(aListener) {
  MOZ_ASSERT(mListener);
}

int CubebStream::Init(cubeb_devid aId, cubeb_stream_params& aParams,
                      uint32_t aLatencyFrames) {
  cubeb* cubebContext = CubebUtils::GetCubebContext();
  if (!cubebContext) {
    LOGE("Can't get cubeb context!");
    CubebUtils::ReportCubebStreamInitFailure(true);
    return CUBEB_ERROR;
  }

  cubeb_stream* stream = nullptr;

  int r = cubeb_stream_init(cubebContext, &stream, "AudioInputStream", aId,
                            &aParams, nullptr, nullptr, aLatencyFrames,
                            DataCallback_S, StateCallback_S, this);

  if (r == CUBEB_OK) {
    mStream.reset(stream);
    CubebUtils::ReportCubebBackendUsed();
  } else {
    CubebUtils::ReportCubebStreamInitFailure(CubebUtils::GetFirstStream());
  }

  return r;
}

int CubebStream::Start() {
  return InvokeCubeb(cubeb_stream_start);
}

int CubebStream::Stop() {
  return InvokeCubeb(cubeb_stream_stop);
}

template <typename Function, typename... Args>
int CubebStream::InvokeCubeb(Function aFunction, Args&&... aArgs) {
  MOZ_ASSERT(mStream);
  return aFunction(mStream.get(), std::forward<Args>(aArgs)...);
}

long CubebStream::DataCallback(void* aBuffer, long aFrames) {
  if (mListener) {
    return mListener->DataCallback(aBuffer, aFrames);
  }
  return aFrames;
}

void CubebStream::StateCallback(cubeb_state aState) {
  if (mListener) {
    mListener->StateCallback(aState);
  }
}

static long CubebStream::DataCallback_s(cubeb_stream* aStream, void* aUser,
                                        const void* aInputBuffer,
                                        void* aOutputBuffer, long aFrames) {
  MOZ_ASSERT(aUser);
  MOZ_ASSERT(aInputBuffer);
  MOZ_ASSERT(!aOutputBuffer); // Now it's input only
  return static_cast<CubebStream*>(aUser)->DataCallback(aInputBuffer, aFrames);
}

static void CubebStream::StateCallback_s(cubeb_stream* aStream, void* aUser,
                                         cubeb_state aState) {
  MOZ_ASSERT(aUser);
  static_cast<CubebStream*>(aUser)->StateCallback(aState);
}

static void CubebStream::DeviceChangedCallback_s(void* aUser) {
  MOZ_ASSERT(aUser);
  static_cast<CubebStream*>(aUser)->DeviceChangedCallback();
}
