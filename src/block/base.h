#ifndef BLOCK_BASE_H
#define BLOCK_BASE_H

// Umbrella for what used to be <base/system.h>.
//
// Upstream split base/system.{h,cpp} into one header per area. Rather than
// spreading that churn over every Block translation unit -- and re-doing it the
// next time upstream reorganises base/ -- Block includes this single header and
// this is the only file that has to be updated.

#include <base/dbg.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/os.h>
#include <base/process.h>
#include <base/secure.h>
#include <base/sphore.h>
#include <base/str.h>
#include <base/thread.h>
#include <base/time.h>
#include <base/types.h>

#endif // BLOCK_BASE_H
