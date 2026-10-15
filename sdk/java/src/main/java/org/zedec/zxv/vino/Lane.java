// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.internal.Errors;

/**
 * The two Vino lanes ({@code ZXV_VINO_LANE_*}). They never meet: nothing in the API takes a play
 * sandbox and something live together, so play units can never become live value.
 */
public enum Lane {
    /**
     * The play lane ({@code ZXV_VINO_LANE_PLAY}, 1): play units inside an app's own
     * {@link PlaySandbox}. Play units are not money, cannot be converted into live value, and are
     * lost when the sandbox closes.
     */
    PLAY(1),
    /**
     * The live lane ({@code ZXV_VINO_LANE_LIVE}, 2): real Vino, held in the user's own ZXV wallet and
     * never in the app. A live {@link PaymentRequest} is amounted in VFV minor units, and a live
     * request for a paid chance outcome is refused ({@link ChanceException}).
     */
    LIVE(2);

    private final int code;

    Lane(int code) {
        this.code = code;
    }

    /**
     * The C value of this lane (the request's lane byte).
     *
     * @return 1 for {@link #PLAY}, 2 for {@link #LIVE}
     */
    public int code() {
        return code;
    }

    /**
     * The lane with a C value.
     *
     * @param code 1 or 2
     * @return the lane
     * @throws LaneException ({@code ZXV_E_LANE}) for any other value
     */
    public static Lane fromCode(int code) {
        for (Lane l : values()) if (l.code == code) return l;
        throw new LaneException(-48, Errors.name(-48), "lane " + code + " is not PLAY (1) or LIVE (2)");
    }
}
