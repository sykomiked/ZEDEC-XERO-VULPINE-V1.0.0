// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// GDExtension entry point (see demo/zxv.gdextension: entry_symbol).
#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "zxv_classes.h"

using namespace godot;

static void initialize_zxv_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    if (zxvgd::check_library() != ZXV_OK)
        UtilityFunctions::push_error(String("ZXV: ") + String(zxvgd::last_error().c_str()));
    GDREGISTER_CLASS(zxv_godot::ZxvRational);
    GDREGISTER_CLASS(zxv_godot::ZxvHexagram);
    GDREGISTER_CLASS(zxv_godot::ZxvLockstep);
}

static void uninitialize_zxv_module(ModuleInitializationLevel p_level) {
    (void) p_level;
}

extern "C" {
GDExtensionBool GDE_EXPORT zxv_godot_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                         GDExtensionClassLibraryPtr p_library,
                                         GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_zxv_module);
    init_obj.register_terminator(uninitialize_zxv_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
