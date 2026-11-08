package com.nxredux.savesync;

// Same rules as workspace/all/savesync/savesync_plan.c. Keep them together.

final class Plan {
    static final int SKIP = 0;
    static final int PUSH = 1;
    static final int PULL = 2;

    private Plan() {}

    // A missing file has size < 0. Times are unix seconds.
    // A 0-byte file never replaces a non-empty one.
    static int decide(long localSize, long localMtime, long remoteSize, long remoteMtime) {
        boolean local = localSize >= 0;
        boolean remote = remoteSize >= 0;
        if (!local && !remote)
            return SKIP;
        if (local && localSize == 0 && remote && remoteSize > 0)
            return PULL;
        if (remote && remoteSize == 0 && local && localSize > 0)
            return PUSH;
        if (local && localSize == 0)
            return SKIP;
        if (remote && remoteSize == 0)
            return SKIP;
        if (!remote)
            return PUSH;
        if (!local)
            return PULL;
        if (localMtime > remoteMtime)
            return PUSH;
        if (remoteMtime > localMtime)
            return PULL;
        return SKIP;
    }

    static boolean pathOk(String rel) {
        if (rel == null || rel.isEmpty() || rel.charAt(0) == '/')
            return false;
        byte[] raw = rel.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        if (raw.length > 480)
            return false;
        for (int i = 0; i < rel.length(); i++) {
            char c = rel.charAt(i);
            if (c < 0x20 || c == '\\' || c == 0x7f)
                return false;
        }
        String[] segs = rel.split("/", -1);
        for (String seg : segs) {
            if (seg.isEmpty() || seg.equals(".") || seg.equals(".."))
                return false;
        }
        return true;
    }

    static String encodePath(String rel) {
        byte[] raw = rel.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        StringBuilder out = new StringBuilder(raw.length * 3);
        for (byte b : raw) {
            int c = b & 0xff;
            boolean plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
            if (plain)
                out.append((char) c);
            else
                out.append(String.format("%%%02X", c));
        }
        return out.toString();
    }

    // Null when the encoding is broken or the path is unsafe.
    static String decodePath(String in) {
        if (in == null)
            return null;
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream(in.length());
        for (int i = 0; i < in.length(); i++) {
            char c = in.charAt(i);
            if (c == '%') {
                if (i + 2 >= in.length())
                    return null;
                int hi = hex(in.charAt(i + 1));
                int lo = hex(in.charAt(i + 2));
                if (hi < 0 || lo < 0)
                    return null;
                out.write((hi << 4) | lo);
                i += 2;
            } else if (c == '+') {
                out.write(' ');
            } else if (c > 127) {
                return null;
            } else {
                out.write(c);
            }
        }
        String rel = new String(out.toByteArray(), java.nio.charset.StandardCharsets.UTF_8);
        return pathOk(rel) ? rel : null;
    }

    private static int hex(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }
}
