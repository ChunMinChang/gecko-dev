/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_netwerk_dns_mdns_libmdns_MDNSResponderOperator_h
#define mozilla_netwerk_dns_mdns_libmdns_MDNSResponderOperator_h

#include "dns_sd.h"
#include "mozilla/Atomics.h"
#include "mozilla/RefCounted.h"
#include "mozilla/RefPtr.h"
#include "nsCOMPtr.h"
#include "nsDeque.h"
#include "nsIDNSServiceDiscovery.h"
#include "nsIThread.h"
#include "nsString.h"

namespace mozilla {
namespace net {


// The PendingSerive will copy the RegisterOperator's parameters into its
// services queue. The stored pending service will start registration after
// receving the reply for notifying the previous service registeration is done.
// RefCounted<T> is a helper class for adding reference counting mechanism.
struct PendingService: public RefCounted<PendingService>
{
  MOZ_DECLARE_REFCOUNTED_TYPENAME(PendingService)

  nsIDNSServiceInfo* mServiceInfo;
  nsIDNSRegistrationListener* mListener;

  PendingService(nsIDNSServiceInfo* aServiceInfo,
                 nsIDNSRegistrationListener* aListener)
    : mServiceInfo(aServiceInfo)
    , mListener(aListener)
  {
  }
};


// The following is the type-safe wrapper around nsDeque
// for storing PendingSerive's data.
// The T must be one class that supports reference counting mechanism.
// The ServiceQueueDeallocator will be called in nsDeque::~nsDeque() or
// nsDeque::Erase() to deallocate the objects. nsDeque::Erase() will remove
// and delete all items in the queue. See more from nsDeque.h.
template <class T>
class ServiceQueueDeallocator : public nsDequeFunctor
{
  virtual void* operator() (void* aObject)
  {
    RefPtr<T> releaseMe = dont_AddRef(static_cast<T*>(aObject));
    return nullptr;
  }
};

// The type-safe queue to be used to store the PendingService's data
template <class T>
class ServiceQueue : private nsDeque
{
public:
  ServiceQueue()
    : nsDeque(new ServiceQueueDeallocator<T>())
  {
  };

  ~ServiceQueue()
  {
    Clear();
  }

  inline size_t GetSize()
  {
    return nsDeque::GetSize();
  }

  bool IsEmpty()
  {
    return !nsDeque::GetSize();
  }

  inline bool Push(T* aItem)
  {
    MOZ_ASSERT(aItem);
    NS_ADDREF(aItem);
    size_t sizeBefore = GetSize();
    nsDeque::Push(aItem);
    if (GetSize() != sizeBefore + 1) {
      NS_RELEASE(aItem);
      return false;
    }
    return true;
  }

  inline already_AddRefed<T> PopFront()
  {
    RefPtr<T> rv = dont_AddRef(static_cast<T*>(nsDeque::PopFront()));
    return rv.forget();
  }

  inline void RemoveFront()
  {
    RefPtr<T> releaseMe = PopFront();
  }

  inline T* PeekFront()
  {
    return static_cast<T*>(nsDeque::PeekFront());
  }

  void Clear()
  {
    while (GetSize() > 0) {
      RemoveFront();
    }
  }
};

class MDNSResponderOperator
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MDNSResponderOperator)

public:
  MDNSResponderOperator();

  virtual nsresult Start();
  virtual nsresult Stop();
  void Cancel() { mIsCancelled = true; }
  nsIThread* GetThread() const { return mThread; }

protected:
  virtual ~MDNSResponderOperator();

  bool IsServing() const { return mService; }
  nsresult ResetService(DNSServiceRef aService);

private:
  class ServiceWatcher;

  DNSServiceRef mService;
  RefPtr<ServiceWatcher> mWatcher;
  nsCOMPtr<nsIThread> mThread; // remember caller thread for callback
  Atomic<bool> mIsCancelled;
};

class BrowseOperator final : public MDNSResponderOperator
{
public:
  BrowseOperator(const nsACString& aServiceType,
                 nsIDNSServiceDiscoveryListener* aListener);

  nsresult Start() override;
  nsresult Stop() override;

  void Reply(DNSServiceRef aSdRef,
             DNSServiceFlags aFlags,
             uint32_t aInterfaceIndex,
             DNSServiceErrorType aErrorCode,
             const nsACString& aServiceName,
             const nsACString& aRegType,
             const nsACString& aReplyDomain);

private:
  ~BrowseOperator() = default;

  nsCString mServiceType;
  nsCOMPtr<nsIDNSServiceDiscoveryListener> mListener;
};

class RegisterOperator final : public MDNSResponderOperator
{
  enum { TXT_BUFFER_SIZE = 256 };

public:
  RegisterOperator(nsIDNSServiceInfo* aServiceInfo,
                   nsIDNSRegistrationListener* aListener);

  nsresult Start() override;
  nsresult Stop() override;

  void Reply(DNSServiceRef aSdRef,
             DNSServiceFlags aFlags,
             DNSServiceErrorType aErrorCode,
             const nsACString& aName,
             const nsACString& aRegType,
             const nsACString& aDomain);

private:
  ~RegisterOperator() = default;
  // Start registering the pending services
  nsresult RunNext();

  nsCOMPtr<nsIDNSServiceInfo> mServiceInfo;
  nsCOMPtr<nsIDNSRegistrationListener> mListener;

  // The service queue is used to store the pending services.
  // Those stored services will be registered after the previous
  // service finishes registration.
  ServiceQueue<PendingService> mServiceQueue;
};

class ResolveOperator final : public MDNSResponderOperator
{
  enum { TXT_BUFFER_SIZE = 256 };

public:
  ResolveOperator(nsIDNSServiceInfo* aServiceInfo,
                  nsIDNSServiceResolveListener* aListener);

  nsresult Start() override;

  void Reply(DNSServiceRef aSdRef,
             DNSServiceFlags aFlags,
             uint32_t aInterfaceIndex,
             DNSServiceErrorType aErrorCode,
             const nsACString& aFullName,
             const nsACString& aHostTarget,
             uint16_t aPort,
             uint16_t aTxtLen,
             const unsigned char* aTxtRecord);

private:
  ~ResolveOperator() = default;
  void GetAddrInfor(nsIDNSServiceInfo* aServiceInfo);

  nsCOMPtr<nsIDNSServiceInfo> mServiceInfo;
  nsCOMPtr<nsIDNSServiceResolveListener> mListener;
};

union NetAddr;

class GetAddrInfoOperator final : public MDNSResponderOperator
{
public:
  GetAddrInfoOperator(nsIDNSServiceInfo* aServiceInfo,
                      nsIDNSServiceResolveListener* aListener);

  nsresult Start() override;

  void Reply(DNSServiceRef aSdRef,
             DNSServiceFlags aFlags,
             uint32_t aInterfaceIndex,
             DNSServiceErrorType aErrorCode,
             const nsACString& aHostName,
             const NetAddr& aAddress,
             uint32_t aTTL);

private:
  ~GetAddrInfoOperator() = default;

  nsCOMPtr<nsIDNSServiceInfo> mServiceInfo;
  nsCOMPtr<nsIDNSServiceResolveListener> mListener;
};

} // namespace net
} // namespace mozilla

#endif // mozilla_netwerk_dns_mdns_libmdns_MDNSResponderOperator_h
