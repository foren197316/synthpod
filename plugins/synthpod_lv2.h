/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _SYNTHPOD_LV2_H
#define _SYNTHPOD_LV2_H

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

// bundle uri
#define SYNTHPOD_URI						"http://open-music-kontrollers.ch/lv2/synthpod"

// plugin uris
#define SYNTHPOD_STEREO_URI			SYNTHPOD_URI"#stereo"

// UI uris
#define SYNTHPOD_COMMON_UI_URI	SYNTHPOD_URI"#common_ui"
#define SYNTHPOD_COMMON_EO_URI	SYNTHPOD_URI"#common_eo"

extern const LV2_Descriptor synthpod_stereo;
extern const LV2UI_Descriptor synthpod_common_ui;
extern const LV2UI_Descriptor synthpod_common_eo;

#endif // _SYNTHPOD_LV2_H
