/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#ifndef TESTSETTINGS_H
#define TESTSETTINGS_H

#ifndef TEST_DURATION
#define TEST_DURATION 0 // Only brief unit tests. < 1 sec
//#  define TEST_DURATION 1  // All unit tests, plus monkey tests. ~1 minute
//#  define TEST_DURATION 2  // Same as 2, but longer monkey tests. 8 minutes
//#  define TEST_DURATION 3
#endif

#define TEST_TABLE

// Takes a long time. Also currently fails to reproduce the Java bug, but once it has been identified, this
// test could perhaps be modified to trigger it (unless it's a language binding problem).
//#define JAVA_MANY_COLUMNS_CRASH

// Temporarily disable async testing until use of sleep() in the async tests have
// been replaced with a better solution.
#define DISABLE_ASYNC

#endif
