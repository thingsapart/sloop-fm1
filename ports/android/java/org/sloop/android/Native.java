/* SPDX-License-Identifier: GPL-3.0-only */
package org.sloop.android;
final class Native {
    static { System.loadLibrary("sloop"); }
    static native void start(String path);
    static native void stop();
    static native boolean restore(byte[] image);
    static native void event(int kind, int id, int value);
    static native void frame(int[] argb);
    static native String status();
    static native void sample(int slot, byte[] image);
}
