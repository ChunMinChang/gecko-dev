// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindate.prototype.tostring
description: If calendarName is "auto", "iso8601" should be omitted.
features: [Temporal]
---*/

const tests = [
  [[], "2000-05-02", "built-in ISO"],
  [["gregory"], "2000-05-02[u-ca=gregory]", "built-in Gregorian"],
];

for (const [args, expected, description] of tests) {
  const date = new Temporal.PlainDate(2000, 5, 2, ...args);
  const result = date.toString({ calendarName: "auto" });
  assert.sameValue(result, expected, `${description} calendar for calendarName = auto`);
}

reportCompare(0, 0);
