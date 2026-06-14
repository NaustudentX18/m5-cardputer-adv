// src/app/capture.h
//
// Capture route: prompt the user for a free-form idea, save it as a
// new project, and jump to the new project's detail screen. Real
// implementation lives in capture.cpp; the dispatcher in routes.cpp
// calls route_capture_impl.

#ifndef ADVDECK_SRC_APP_CAPTURE_H_
#define ADVDECK_SRC_APP_CAPTURE_H_

#include "app/routes.h"

namespace advdeck {
namespace app {

// Real implementation of the Capture route. On confirm, creates a new
// project and returns Route::ProjectDetail so the dispatcher can chain
// to the new slug. On cancel returns Route::Home. On create failure
// (e.g. SD card missing), shows the error and re-enters the editor
// until the user cancels.
Route route_capture_impl(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_CAPTURE_H_
