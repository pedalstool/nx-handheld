package com.nxredux.savesync;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;

// The save tree. Paths are relative to the folder the user picked, with '/'.
final class Store {
    static final long MAX_FILE = 32L * 1024L * 1024L;
    private static final Object LOCK = new Object();

    static final class Entry {
        final String path;
        final long mtime;
        final long size;

        Entry(String path, long mtime, long size) {
            this.path = path;
            this.mtime = mtime;
            this.size = size;
        }
    }

    static final class FileBytes {
        final byte[] data;
        final long mtime;

        FileBytes(byte[] data, long mtime) {
            this.data = data;
            this.mtime = mtime;
        }
    }

    private Store() {}

    static List<Entry> scan(File root) {
        List<Entry> out = new ArrayList<>();
        if (root != null && root.isDirectory())
            scanDir(root, root.toPath(), out);
        return out;
    }

    static Entry find(List<Entry> list, String path) {
        for (Entry e : list) {
            if (e.path.equals(path))
                return e;
        }
        return null;
    }

    static FileBytes read(File root, String rel) {
        synchronized (LOCK) {
            if (!Plan.pathOk(rel))
                return null;
            File file = new File(root, rel);
            try {
                Path path = file.toPath();
                if (Files.isSymbolicLink(path) || !Files.isRegularFile(path))
                    return null;
                long size = Files.size(path);
                if (size < 0 || size > MAX_FILE)
                    return null;
                byte[] data = Files.readAllBytes(path);
                long mtime = Files.getLastModifiedTime(path).toMillis() / 1000L;
                return new FileBytes(data, mtime);
            } catch (IOException e) {
                return null;
            }
        }
    }

    // Keeps the source mtime. Stamping "now" would make the copy look newer
    // and bounce straight back.
    static boolean write(File root, String rel, byte[] data, long mtime) {
        synchronized (LOCK) {
            if (!Plan.pathOk(rel) || data == null || data.length > MAX_FILE || mtime < 0)
                return false;
            File dest = new File(root, rel);
            File parent = dest.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                return false;
            try {
                if (Files.isSymbolicLink(dest.toPath()))
                    return false;
            } catch (Exception ignored) {
                return false;
            }
            File tmp = new File(dest.getAbsolutePath() + ".savesync-tmp");
            FileOutputStream out = null;
            try {
                out = new FileOutputStream(tmp);
                out.write(data);
                out.getFD().sync();
                out.close();
                out = null;
                if (dest.exists() && !dest.delete()) {
                    tmp.delete();
                    return false;
                }
                if (!tmp.renameTo(dest)) {
                    tmp.delete();
                    return false;
                }
                dest.setLastModified(mtime * 1000L);
                return true;
            } catch (IOException e) {
                if (out != null) {
                    try {
                        out.close();
                    } catch (IOException ignored) {
                    }
                }
                tmp.delete();
                return false;
            }
        }
    }

    private static void scanDir(File root, Path dir, List<Entry> out) {
        File[] kids = dir.toFile().listFiles();
        if (kids == null)
            return;
        for (File kid : kids) {
            String name = kid.getName();
            if (name.startsWith(".") || name.endsWith(".savesync-tmp"))
                continue;
            Path path = kid.toPath();
            BasicFileAttributes attr;
            try {
                attr = Files.readAttributes(path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
            } catch (IOException e) {
                continue;
            }
            if (attr.isSymbolicLink())
                continue;
            String rel = root.toPath().relativize(path).toString().replace('\\', '/');
            if (!Plan.pathOk(rel))
                continue;
            if (attr.isDirectory()) {
                scanDir(root, path, out);
                continue;
            }
            if (!attr.isRegularFile())
                continue;
            out.add(new Entry(rel, attr.lastModifiedTime().toMillis() / 1000L, attr.size()));
        }
    }
}
