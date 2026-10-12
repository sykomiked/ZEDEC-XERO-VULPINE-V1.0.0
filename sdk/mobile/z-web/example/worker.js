// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The example's worker, with relative imports so it runs without a
// bundler (module workers do not see the page's import map). In an app,
// use `@zedec/zxv-web/worker` through your bundler instead.

import * as sdk from '../../../typescript/dist/src/browser.js';
import { serve } from '../dist/src/worker-core.js';

serve(self, sdk);
