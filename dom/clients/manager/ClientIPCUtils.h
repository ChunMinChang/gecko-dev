/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _mozilla_dom_ClientIPCUtils_h
#define _mozilla_dom_ClientIPCUtils_h

#include "ipc/EnumSerializer.h"

#include "X11UndefineNone.h"
#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/ClientBinding.h"
#include "mozilla/dom/ClientsBinding.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/StorageAccess.h"

namespace IPC {
template <>
struct ParamTraits<mozilla::dom::ClientType>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::ClientType> {};

template <>
struct ParamTraits<mozilla::dom::FrameType>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::FrameType> {};

template <>
struct ParamTraits<mozilla::dom::VisibilityState>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::VisibilityState> {
};

template <>
struct ParamTraits<mozilla::StorageAccess>
    : public ContiguousEnumSerializer<
          mozilla::StorageAccess,
          mozilla::StorageAccess::ePartitionForeignOrDeny,
          mozilla::StorageAccess::eNumValues> {};
}  // namespace IPC

#endif  // _mozilla_dom_ClientIPCUtils_h
