// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaintime.from
description: Fast path for converting Temporal.PlainDateTime to Temporal.PlainTime by reading internal slots
includes: [compareArray.js, temporalHelpers.js]
features: [Temporal]
---*/

TemporalHelpers.checkPlainDateTimeConversionFastPath((plainDateTime) => {
  const result = Temporal.PlainTime.from(plainDateTime);
  TemporalHelpers.assertPlainTime(result, 12, 34, 56, 987, 654, 321);
});

reportCompare(0, 0);
