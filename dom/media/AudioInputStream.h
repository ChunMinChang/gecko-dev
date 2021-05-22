#include "CubebUtils.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/SPSCQueue.h"

// For input-device only now
class CubebStream {
 public:
  class Listener {
    public:
     long DataCallback(void* aBuffer, long aFrames);
     void StateCallback(cubeb_state aState);
  };

  // Please make sure aListener lives longer than CubebStream
  explicit CubebStream(Listener* aListener);
  ~CubebStream() = default;

  int Init(cubeb_devid aId, cubeb_stream_params aParams,
           uint32_t aLatencyFrames);
  int Start();
  int Stop();

 private:
  template <typename Function, typename... Args>
  int InvokeCubeb(Function aFunction, Args&&... aArgs);

  long DataCallback(void* aBuffer, long aFrames);

  void StateCallback(cubeb_state aState);

  // Static wrapper function cubeb callbacks
  static long DataCallback_s(cubeb_stream* aStream, void* aUser,
                             const void* aInputBuffer, void* aOutputBuffer,
                             long aFrames);
  static void StateCallback_s(cubeb_stream* aStream, void* aUser,
                              cubeb_state aState);
  static void DeviceChangedCallback_s(void* aUser);

  struct CubebDestroyPolicy {
    void operator()(cubeb_stream* aStream) const {
      cubeb_stream_destroy(aStream);
    }
  };
  UniquePtr<cubeb_stream, CubebDestroyPolicy> mStream;

  // Please make sure mListener lives longer than CubebStream
  Listener* mListener;
}

template <class T>
class AudioInpuStream final: private CubebStream::Listener {
 public:
  AudioInpuStream(cubeb_devid aId, uint32_t rate, uint32_t channels);
  ~AudioInpuStream();

  nsresult Open();
  nsresult Close();

  nsresult Start();
  nsresult Stop();

 private:
  // CubebStream::Listener Interface
  long DataCallback(void* aBuffer, long aFrames);
  void StateCallback(cubeb_state aState);

  struct SPSCData final {
    UniquePtr<T> mData;
    uint32_t mChannels;
    long frames;
  };
  SPSCQueue<SPSCData> mSPSCQueue{100};

  UniquePtr<CubebStream> mCubebStream;
};