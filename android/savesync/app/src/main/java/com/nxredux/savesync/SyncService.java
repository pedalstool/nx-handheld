package com.nxredux.savesync;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.wifi.WifiManager;
import android.os.Environment;
import android.os.IBinder;
import android.os.PowerManager;

import java.io.File;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class SyncService extends Service {
    public static final String ACTION_START = "com.nxredux.savesync.START";
    public static final String ACTION_STOP = "com.nxredux.savesync.STOP";
    public static final String ACTION_SYNC = "com.nxredux.savesync.SYNC";

    static final String PREFS = "savesync";
    static final String KEY_PEER = "peer";
    static final String KEY_ROOT = "root";
    static final String KEY_ON = "on";
    static final String DEFAULT_ROOT = "/storage/emulated/0/RetroArch/saves";

    public interface StatusListener {
        void onStatus(String status);
    }

    public static volatile String status = "OFF";
    public static volatile StatusListener listener;

    private static final String CHANNEL = "sync";
    private static final int NOTE_ID = 1;

    private volatile boolean running;
    private volatile boolean syncNow;
    private boolean foreground;
    private Thread serverThread;
    private Thread pollThread;
    private WifiManager.WifiLock wifi;
    private PowerManager.WakeLock wake;

    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null && intent.getAction() != null ? intent.getAction() : ACTION_START;
        if (ACTION_STOP.equals(action)) {
            running = false;
            if (foreground)
                stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            publish("OFF");
            return START_NOT_STICKY;
        }
        ensureChannel();
        startForeground(NOTE_ID, note(status));
        foreground = true;
        ensureLocks();
        if (ACTION_SYNC.equals(action))
            syncNow = true;
        ensureThreads();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        if (serverThread != null)
            serverThread.interrupt();
        if (pollThread != null)
            pollThread.interrupt();
        if (wifi != null && wifi.isHeld())
            wifi.release();
        super.onDestroy();
    }

    private void ensureThreads() {
        if (running && serverThread != null && serverThread.isAlive())
            return;
        running = true;
        serverThread = new Thread(this::serveLoop, "savesync-serve");
        pollThread = new Thread(this::pollLoop, "savesync-poll");
        serverThread.start();
        pollThread.start();
    }

    private void ensureLocks() {
        if (wifi == null) {
            WifiManager wm = (WifiManager) getSystemService(WIFI_SERVICE);
            wifi = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "savesync");
            wifi.setReferenceCounted(false);
        }
        if (!wifi.isHeld())
            wifi.acquire();
        if (wake == null) {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "savesync:sync");
            wake.setReferenceCounted(true);
        }
    }

    private void serveLoop() {
        ServerSocket server = null;
        while (running) {
            try {
                if (server == null)
                    server = Http.listen();
                Socket client = server.accept();
                boolean held = false;
                try {
                    if (wake != null) {
                        wake.acquire();
                        held = true;
                    }
                    Http.serveOne(client, root());
                } catch (Exception ignored) {
                    // one bad request stays off the accept loop
                } finally {
                    try {
                        client.close();
                    } catch (Exception ignored) {
                    }
                    if (held)
                        wake.release();
                }
            } catch (SocketTimeoutException ignored) {
                // accept woke up so we can notice a stop
            } catch (Exception e) {
                if (server != null) {
                    try {
                        server.close();
                    } catch (Exception ignored) {
                    }
                    server = null;
                }
                sleepQuiet(2000);
            }
        }
        if (server != null) {
            try {
                server.close();
            } catch (Exception ignored) {
            }
        }
    }

    private void pollLoop() {
        Map<String, Long> stable = new HashMap<>();
        while (running) {
            boolean now = syncNow;
            syncNow = false;
            boolean held = false;
            try {
                if (wake != null) {
                    wake.acquire();
                    held = true;
                }
                publish(exchange(stable, !now));
            } catch (Exception e) {
                publish("BRICK UNREACHABLE");
            } finally {
                if (held)
                    wake.release();
            }
            for (int i = 0; i < 40 && running && !syncNow; i++)
                sleepQuiet(100);
        }
    }

    // requireStable skips a push until the file's time has sat still for one
    // poll, so a save still being written is not uploaded halfway.
    private String exchange(Map<String, Long> stable, boolean requireStable) throws Exception {
        String peer = prefs(this).getString(KEY_PEER, "");
        if (!ipv4(peer))
            return "SET THE BRICK IP";
        File dir = root();
        if (!dir.isDirectory() && !dir.mkdirs())
            return "CANNOT OPEN FOLDER";
        if (!Environment.isExternalStorageManager())
            return "ALLOW ALL FILES";
        List<Store.Entry> local = Store.scan(dir);
        List<Http.RemoteFile> remote = Http.fetchIndex(peer);
        int pushed = 0;
        int pulled = 0;
        int held = 0;
        Map<String, Long> next = new HashMap<>();
        for (Store.Entry e : local) {
            next.put(e.path, e.mtime);
            Http.RemoteFile r = findRemote(remote, e.path);
            long rsize = r == null ? -1 : r.size;
            long rmtime = r == null ? -1 : r.mtime;
            if (Plan.decide(e.size, e.mtime, rsize, rmtime) != Plan.PUSH)
                continue;
            if (requireStable) {
                Long prev = stable.get(e.path);
                if (prev == null || prev.longValue() != e.mtime)
                    continue;
            }
            Store.FileBytes file = Store.read(dir, e.path);
            if (file == null)
                continue;
            if (Plan.decide(file.data.length, file.mtime, rsize, rmtime) != Plan.PUSH)
                continue;
            int up = Http.upload(peer, e.path, file.data, file.mtime);
            if (up == 0)
                pushed++;
            else if (up == 1)
                held++;
        }
        for (Http.RemoteFile e : remote) {
            Store.Entry loc = Store.find(local, e.path);
            long lsize = loc == null ? -1 : loc.size;
            long lmtime = loc == null ? 0 : loc.mtime;
            if (Plan.decide(lsize, lmtime, e.size, e.mtime) != Plan.PULL)
                continue;
            Http.Got got = Http.download(peer, e.path);
            long mt = got.mtime >= 0 ? got.mtime : e.mtime;
            if (Plan.decide(lsize, lmtime, got.data.length, mt) != Plan.PULL)
                continue;
            if (Store.write(dir, e.path, got.data, mt)) {
                pulled++;
                next.put(e.path, mt);
            }
        }
        stable.clear();
        stable.putAll(next);
        if (held > 0)
            return "SENT " + pushed + "  GOT " + pulled + "  HELD " + held;
        return "SENT " + pushed + "  GOT " + pulled;
    }

    private File root() {
        String path = prefs(this).getString(KEY_ROOT, DEFAULT_ROOT);
        if (path == null || path.isEmpty())
            path = DEFAULT_ROOT;
        return new File(path);
    }

    private static Http.RemoteFile findRemote(List<Http.RemoteFile> list, String path) {
        for (Http.RemoteFile e : list) {
            if (e.path.equals(path))
                return e;
        }
        return null;
    }

    static boolean ipv4(String s) {
        return canonical(s) != null;
    }

    // Dotted form with no leading zeros, or null. 0.0.0.0 is unset.
    static String canonical(String s) {
        if (s == null)
            return null;
        String[] p = s.trim().split("\\.");
        if (p.length != 4)
            return null;
        int[] n = new int[4];
        for (int i = 0; i < 4; i++) {
            if (p[i].isEmpty() || p[i].length() > 3)
                return null;
            for (int c = 0; c < p[i].length(); c++) {
                if (p[i].charAt(c) < '0' || p[i].charAt(c) > '9')
                    return null;
            }
            n[i] = Integer.parseInt(p[i]);
            if (n[i] > 255)
                return null;
        }
        String ip = n[0] + "." + n[1] + "." + n[2] + "." + n[3];
        if (ip.equals("0.0.0.0"))
            return null;
        return ip;
    }

    private void publish(String text) {
        status = text;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm != null && running)
            nm.notify(NOTE_ID, note(text));
        StatusListener current = listener;
        if (current != null)
            current.onStatus(text);
    }

    private void ensureChannel() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        NotificationChannel channel = new NotificationChannel(CHANNEL, "Save Sync", NotificationManager.IMPORTANCE_LOW);
        nm.createNotificationChannel(channel);
    }

    private Notification note(String text) {
        return new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_notify_sync)
                .setContentTitle("Save Sync")
                .setContentText(text == null || text.isEmpty() ? "ON" : text)
                .setOngoing(true)
                .build();
    }

    private static void sleepQuiet(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
