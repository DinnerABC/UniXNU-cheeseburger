// SPDX-License-Identifier: MIT
// P21-UP1: selected development RAM-Data job policy, Pagani 02c7ccd.
#pragma once
static const char cheeseburger_watchdog_xml[]=
 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
 "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
 "<plist version=\"1.0\"><dict><key>com.apple.watchdogd</key><true/></dict></plist>\n";
static const char cheeseburger_upstream_xml[]=
 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
 "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
 "<plist version=\"1.0\"><dict><key>com.apple.watchdogd</key><true/><key>com.apple.identityservicesd</key><true/><key>com.apple.locationd</key><true/></dict></plist>\n";
static inline unsigned cheeseburger_policy_mask(int watchdog, int upstream) {
#if DEVELOPMENT || DEBUG
 return upstream==1 ? 7u : (watchdog==1 ? 1u : 0u);
#else
 (void)watchdog;(void)upstream;return 0;
#endif
}
