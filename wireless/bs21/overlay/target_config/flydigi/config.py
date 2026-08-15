#!/usr/bin/env python3
# encoding=utf-8
# =========================================================================
# @brief    Flydigi BS21 target definitions.
#
# The Flydigi board is based on HiSilicon BS21E (Ai-Thinker Ai-BS21-32S-Kit).
# This target derives from standard-bs21e-1100e and adds only board-specific
# diffs, leaving the upstream bs21e target untouched.
#
# Copyright (c) 2026 Flydigi Receiver Project. All rights reserved.
# =========================================================================

target = {
    'flydigi-bs21e': {
        'base_target_name': 'standard-bs21e-1100e',
    },
}

target_copy = {}

target_group = {}
