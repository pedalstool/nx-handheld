package com.nxredux.savesync;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()))
            return;
        if (!SyncService.prefs(context).getBoolean(SyncService.KEY_ON, false))
            return;
        Intent start = new Intent(context, SyncService.class);
        start.setAction(SyncService.ACTION_START);
        context.startForegroundService(start);
    }
}
