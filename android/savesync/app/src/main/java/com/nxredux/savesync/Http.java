package com.nxredux.savesync;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

// HTTP/1.0 on port 27790. Matches workspace/all/savesync/savesync_net.c.
final class Http {
    static final int PORT = 27790;

    private Http() {}

    static final class RemoteFile {
        final String path;
        final long mtime;
        final long size;

        RemoteFile(String path, long mtime, long size) {
            this.path = path;
            this.mtime = mtime;
            this.size = size;
        }
    }

    static final class Got {
        final byte[] data;
        final long mtime;

        Got(byte[] data, long mtime) {
            this.data = data;
            this.mtime = mtime;
        }
    }

    static List<RemoteFile> fetchIndex(String host) throws IOException {
        Response res = exchange(host, "GET /index HTTP/1.0\r\nHost: savesync\r\nConnection: close\r\n\r\n", null);
        if (res.code != 200)
            throw new IOException("index " + res.code);
        String text = new String(res.body, StandardCharsets.UTF_8);
        List<RemoteFile> out = new ArrayList<>();
        int i = 0;
        while (i < text.length()) {
            int nl = text.indexOf('\n', i);
            String line = nl < 0 ? text.substring(i) : text.substring(i, nl);
            i = nl < 0 ? text.length() : nl + 1;
            if (line.endsWith("\r"))
                line = line.substring(0, line.length() - 1);
            if (line.isEmpty())
                continue;
            RemoteFile row = parseLine(line);
            if (row != null)
                out.add(row);
        }
        return out;
    }

    static Got download(String host, String rel) throws IOException {
        String req = "GET /file?p=" + Plan.encodePath(rel) + " HTTP/1.0\r\nHost: savesync\r\nConnection: close\r\n\r\n";
        Response res = exchange(host, req, null);
        if (res.code != 200)
            throw new IOException("get " + res.code);
        return new Got(res.body, res.mtime);
    }

    // 0 stored, 1 peer kept its newer file, -1 error.
    static int upload(String host, String rel, byte[] data, long mtime) {
        try {
            String head = "PUT /file?p=" + Plan.encodePath(rel)
                    + " HTTP/1.0\r\nHost: savesync\r\nContent-Length: " + data.length
                    + "\r\nX-Mtime: " + mtime + "\r\nConnection: close\r\n\r\n";
            Response res = exchange(host, head, data);
            if (res.code == 204)
                return 0;
            if (res.code == 409)
                return 1;
            return -1;
        } catch (IOException e) {
            return -1;
        }
    }

    static ServerSocket listen() throws IOException {
        ServerSocket server = new ServerSocket();
        server.setReuseAddress(true);
        server.bind(new InetSocketAddress(PORT));
        server.setSoTimeout(1000);
        return server;
    }

    static void serveOne(Socket socket, java.io.File root) throws IOException {
        socket.setSoTimeout(20000);
        InputStream in = socket.getInputStream();
        byte[] raw = readHeaders(in);
        if (raw == null)
            return;
        int split = indexOf(raw, "\r\n\r\n");
        if (split < 0)
            return;
        String head = new String(raw, 0, split, StandardCharsets.ISO_8859_1);
        int bodyHave = raw.length - (split + 4);
        String[] lines = head.split("\r\n");
        if (lines.length == 0)
            return;
        String[] req = lines[0].split(" ");
        if (req.length < 2) {
            send(socket, 400, "Bad Request", null, null);
            return;
        }
        if (!android.os.Environment.isExternalStorageManager()) {
            send(socket, 403, "Forbidden", null, null);
            return;
        }
        String method = req[0];
        String target = req[1];
        long contentLength = 0;
        long mtime = -1;
        boolean sawLength = false;
        for (int i = 1; i < lines.length; i++) {
            String line = lines[i];
            int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            String key = line.substring(0, colon).trim();
            String val = line.substring(colon + 1).trim();
            if (key.equalsIgnoreCase("Content-Length")) {
                contentLength = parseLong(val, 0);
                sawLength = true;
            } else if (key.equalsIgnoreCase("X-Mtime")) {
                mtime = parseLong(val, -1);
            }
        }
        if (!sawLength)
            contentLength = 0;
        String pathOnly = target;
        int q = pathOnly.indexOf('?');
        if (q >= 0)
            pathOnly = pathOnly.substring(0, q);

        if (method.equals("GET") && pathOnly.equals("/index")) {
            StringBuilder body = new StringBuilder();
            for (Store.Entry e : Store.scan(root))
                body.append(e.mtime).append('\t').append(e.size).append('\t').append(e.path).append('\n');
            send(socket, 200, "OK", "Content-Type: text/plain\r\n", body.toString().getBytes(StandardCharsets.UTF_8));
            return;
        }

        String rel = queryPath(target);
        if (rel == null) {
            send(socket, 400, "Bad Request", null, null);
            return;
        }
        if (method.equals("GET") && pathOnly.equals("/file")) {
            Store.FileBytes file = Store.read(root, rel);
            if (file == null) {
                send(socket, 404, "Not Found", null, null);
                return;
            }
            send(socket, 200, "OK", "X-Mtime: " + file.mtime + "\r\n", file.data);
            return;
        }
        if (method.equals("PUT") && pathOnly.equals("/file")) {
            if (contentLength < 0 || contentLength > Store.MAX_FILE) {
                send(socket, 400, "Bad Request", null, null);
                return;
            }
            byte[] body = readBody(in, raw, split + 4, bodyHave, (int) contentLength);
            if (body == null) {
                send(socket, 400, "Bad Request", null, null);
                return;
            }
            java.io.File dest = new java.io.File(root, rel);
            long localSize = -1;
            long localMtime = 0;
            if (dest.isFile() && !java.nio.file.Files.isSymbolicLink(dest.toPath())) {
                localSize = dest.length();
                localMtime = dest.lastModified() / 1000L;
            }
            if (Plan.decide(localSize, localMtime, contentLength, mtime) != Plan.PULL) {
                send(socket, 409, "Conflict", null, null);
                return;
            }
            if (mtime < 0)
                mtime = localMtime + 1;
            if (!Store.write(root, rel, body, mtime)) {
                send(socket, 500, "Error", null, null);
                return;
            }
            send(socket, 204, "No Content", null, null);
            return;
        }
        send(socket, 404, "Not Found", null, null);
    }

    private static Response exchange(String host, String head, byte[] body) throws IOException {
        Socket socket = new Socket();
        try {
            socket.connect(new InetSocketAddress(host, PORT), 2000);
            socket.setSoTimeout(8000);
            OutputStream out = socket.getOutputStream();
            out.write(head.getBytes(StandardCharsets.ISO_8859_1));
            if (body != null && body.length > 0)
                out.write(body);
            out.flush();
            InputStream in = socket.getInputStream();
            byte[] raw = readHeaders(in);
            if (raw == null)
                throw new IOException("no headers");
            int split = indexOf(raw, "\r\n\r\n");
            if (split < 0)
                throw new IOException("bad headers");
            String headText = new String(raw, 0, split, StandardCharsets.ISO_8859_1);
            int code = statusCode(headText);
            long length = headerLong(headText, "Content-Length", 0);
            long mtime = headerLong(headText, "X-Mtime", -1);
            int have = raw.length - (split + 4);
            byte[] got = readBody(in, raw, split + 4, have, (int) length);
            if (got == null)
                throw new IOException("short body");
            return new Response(code, got, mtime);
        } finally {
            try {
                socket.close();
            } catch (IOException ignored) {
            }
        }
    }

    private static final class Response {
        final int code;
        final byte[] body;
        final long mtime;

        Response(int code, byte[] body, long mtime) {
            this.code = code;
            this.body = body;
            this.mtime = mtime;
        }
    }

    private static RemoteFile parseLine(String line) {
        int t1 = line.indexOf('\t');
        int t2 = t1 < 0 ? -1 : line.indexOf('\t', t1 + 1);
        if (t1 < 0 || t2 < 0)
            return null;
        long mtime;
        long size;
        try {
            mtime = Long.parseLong(line.substring(0, t1).trim());
            size = Long.parseLong(line.substring(t1 + 1, t2).trim());
        } catch (NumberFormatException e) {
            return null;
        }
        String path = line.substring(t2 + 1);
        if (size < 0 || !Plan.pathOk(path))
            return null;
        return new RemoteFile(path, mtime, size);
    }

    private static String queryPath(String target) {
        int q = target.indexOf('?');
        if (q < 0)
            return null;
        String query = target.substring(q + 1);
        for (String part : query.split("&")) {
            if (part.startsWith("p="))
                return Plan.decodePath(part.substring(2));
        }
        return null;
    }

    private static byte[] readHeaders(InputStream in) throws IOException {
        ByteArrayOutputStream acc = new ByteArrayOutputStream();
        byte[] tmp = new byte[1024];
        while (acc.size() < 8192) {
            int n = in.read(tmp);
            if (n < 0)
                break;
            if (n == 0)
                continue;
            acc.write(tmp, 0, n);
            if (indexOf(acc.toByteArray(), "\r\n\r\n") >= 0)
                return acc.toByteArray();
        }
        return null;
    }

    private static byte[] readBody(InputStream in, byte[] raw, int bodyAt, int have, int length) throws IOException {
        if (length < 0 || length > Store.MAX_FILE)
            return null;
        byte[] body = new byte[length];
        int fromBuf = Math.min(have, length);
        if (fromBuf > 0)
            System.arraycopy(raw, bodyAt, body, 0, fromBuf);
        int got = fromBuf;
        while (got < length) {
            int n = in.read(body, got, length - got);
            if (n < 0)
                return null;
            got += n;
        }
        return body;
    }

    private static void send(Socket socket, int code, String reason, String extra, byte[] body) throws IOException {
        if (body == null)
            body = new byte[0];
        String head = "HTTP/1.0 " + code + " " + reason + "\r\nContent-Length: " + body.length
                + "\r\nConnection: close\r\n" + (extra == null ? "" : extra) + "\r\n";
        OutputStream out = socket.getOutputStream();
        out.write(head.getBytes(StandardCharsets.ISO_8859_1));
        if (body.length > 0)
            out.write(body);
        out.flush();
    }

    private static int statusCode(String head) {
        int sp = head.indexOf(' ');
        if (sp < 0)
            return 0;
        int sp2 = head.indexOf(' ', sp + 1);
        String num = sp2 < 0 ? head.substring(sp + 1) : head.substring(sp + 1, sp2);
        return (int) parseLong(num, 0);
    }

    private static long headerLong(String head, String name, long fallback) {
        String[] lines = head.split("\r\n");
        for (int i = 1; i < lines.length; i++) {
            int colon = lines[i].indexOf(':');
            if (colon < 0)
                continue;
            if (lines[i].substring(0, colon).trim().equalsIgnoreCase(name))
                return parseLong(lines[i].substring(colon + 1).trim(), fallback);
        }
        return fallback;
    }

    private static long parseLong(String s, long fallback) {
        try {
            return Long.parseLong(s.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static int indexOf(byte[] data, String needle) {
        byte[] n = needle.getBytes(StandardCharsets.ISO_8859_1);
        outer:
        for (int i = 0; i + n.length <= data.length; i++) {
            for (int j = 0; j < n.length; j++) {
                if (data[i + j] != n[j])
                    continue outer;
            }
            return i;
        }
        return -1;
    }
}
