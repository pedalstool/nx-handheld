package com.nxredux.savesync;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.PowerManager;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import java.io.File;
import java.net.Inet4Address;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;

public class MainActivity extends Activity {
    private static final int REQ_NOTES = 1;

    private TextView selfIp;
    private TextView status;
    private EditText peer;
    private EditText folder;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        selfIp = findViewById(R.id.self_ip);
        status = findViewById(R.id.status);
        peer = findViewById(R.id.peer);
        folder = findViewById(R.id.folder);

        Button start = findViewById(R.id.start);
        Button sync = findViewById(R.id.sync);
        Button stop = findViewById(R.id.stop);
        Button retro = findViewById(R.id.retroarch);
        Button browse = findViewById(R.id.browse);
        start.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                begin(SyncService.ACTION_START);
            }
        });
        sync.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                begin(SyncService.ACTION_SYNC);
            }
        });
        stop.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                save(false);
                startService(new Intent(MainActivity.this, SyncService.class).setAction(SyncService.ACTION_STOP));
            }
        });
        retro.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                folder.setText(SyncService.DEFAULT_ROOT);
            }
        });
        browse.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File dir = new File(folder.getText().toString().trim());
                if (!dir.isDirectory())
                    dir = Environment.getExternalStorageDirectory();
                browse(dir);
            }
        });

        if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTES);
        if (!Environment.isExternalStorageManager()) {
            Intent access = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            access.setData(Uri.parse("package:" + getPackageName()));
            startActivity(access);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        selfIp.setText(selfAddress());
        String savedPeer = SyncService.prefs(this).getString(SyncService.KEY_PEER, "");
        String savedRoot = SyncService.prefs(this).getString(SyncService.KEY_ROOT, SyncService.DEFAULT_ROOT);
        if (peer.getText().length() == 0)
            peer.setText(savedPeer);
        if (folder.getText().length() == 0)
            folder.setText(savedRoot == null || savedRoot.isEmpty() ? SyncService.DEFAULT_ROOT : savedRoot);
        status.setText(SyncService.status);
        SyncService.listener = new SyncService.StatusListener() {
            @Override
            public void onStatus(final String text) {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        status.setText(text);
                    }
                });
            }
        };
    }

    @Override
    protected void onPause() {
        save(SyncService.prefs(this).getBoolean(SyncService.KEY_ON, false));
        SyncService.listener = null;
        super.onPause();
    }

    private void begin(String action) {
        if (SyncService.canonical(peer.getText().toString()) == null) {
            status.setText("SET THE BRICK IP");
            return;
        }
        save(true);
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        if (!pm.isIgnoringBatteryOptimizations(getPackageName())) {
            try {
                Intent battery = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
                battery.setData(Uri.parse("package:" + getPackageName()));
                startActivity(battery);
            } catch (Exception ignored) {
            }
        }
        Intent intent = new Intent(this, SyncService.class);
        intent.setAction(action);
        startForegroundService(intent);
    }

    private void save(boolean on) {
        String ip = SyncService.canonical(peer.getText().toString());
        String path = folder.getText().toString().trim();
        if (path.isEmpty())
            path = SyncService.DEFAULT_ROOT;
        SyncService.prefs(this).edit()
                .putString(SyncService.KEY_PEER, ip == null ? "" : ip)
                .putString(SyncService.KEY_ROOT, path)
                .putBoolean(SyncService.KEY_ON, on)
                .apply();
    }

    private void browse(final File dir) {
        File[] kids = dir.listFiles();
        final List<File> dirs = new ArrayList<>();
        if (kids != null) {
            for (File kid : kids) {
                if (kid.isDirectory() && !kid.getName().startsWith("."))
                    dirs.add(kid);
            }
        }
        Collections.sort(dirs);
        String[] names = new String[dirs.size() + 1];
        names[0] = "Use this folder";
        for (int i = 0; i < dirs.size(); i++)
            names[i + 1] = dirs.get(i).getName() + "/";
        new AlertDialog.Builder(this)
                .setTitle(dir.getAbsolutePath())
                .setItems(names, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface dialog, int which) {
                        if (which == 0)
                            folder.setText(dir.getAbsolutePath());
                        else
                            browse(dirs.get(which - 1));
                    }
                })
                .setNegativeButton("Up", new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface dialog, int which) {
                        File parent = dir.getParentFile();
                        if (parent != null)
                            browse(parent);
                    }
                })
                .show();
    }

    private static String selfAddress() {
        String fallback = "";
        try {
            Enumeration<NetworkInterface> ifs = NetworkInterface.getNetworkInterfaces();
            while (ifs != null && ifs.hasMoreElements()) {
                NetworkInterface nif = ifs.nextElement();
                if (nif.isLoopback())
                    continue;
                Enumeration<java.net.InetAddress> addrs = nif.getInetAddresses();
                while (addrs.hasMoreElements()) {
                    java.net.InetAddress addr = addrs.nextElement();
                    if (!(addr instanceof Inet4Address))
                        continue;
                    String ip = addr.getHostAddress();
                    if (ip == null || ip.startsWith("169.254."))
                        continue;
                    if (addr.isSiteLocalAddress())
                        return ip;
                    if (fallback.isEmpty())
                        fallback = ip;
                }
            }
        } catch (Exception ignored) {
        }
        return fallback.isEmpty() ? "NO WIFI" : fallback;
    }
}
