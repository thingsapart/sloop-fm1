/* SPDX-License-Identifier: GPL-3.0-only */
package org.sloop.android;
import android.app.*;
import android.content.*;
import android.media.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.file.*;

public final class MainActivity extends Activity {
    private SloopView instrument;
    private MidiInputs midi;
    private TextView status;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private AudioManager audio;
    private AudioFocusRequest focus;
    private boolean resumed, started;
    private int sampleSlot;
    private File storage;
    private final Runnable update=new Runnable(){public void run(){if(!resumed)return;status.setText(Native.status());handler.postDelayed(this,1000);}};
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        storage=new File(getFilesDir(),"sloop-flash.bin");
        LinearLayout layout=new LinearLayout(this);
        layout.setOnApplyWindowInsetsListener((view,insets)->{
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());view.setPadding(bars.left,bars.top,bars.right,bars.bottom);}
            else view.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            return insets;
        });
        layout.setOrientation(LinearLayout.VERTICAL);layout.setBackgroundColor(0xff101216);
        LinearLayout bar=new LinearLayout(this);Button menu=new Button(this);menu.setText("SLOOP · MIDI / Files");menu.setOnClickListener(v->menu());bar.addView(menu);
        status=new TextView(this);status.setTextColor(0xffcccccc);status.setTextSize(11);bar.addView(status,new LinearLayout.LayoutParams(0,-2,1));layout.addView(bar);
        instrument=new SloopView(this);layout.addView(instrument,new LinearLayout.LayoutParams(-1,0,1));setContentView(layout);
        midi=new MidiInputs(this);midi.restorePreferences();audio=(AudioManager)getSystemService(AUDIO_SERVICE);
        focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(
            new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(change->{
                if(change==AudioManager.AUDIOFOCUS_GAIN){if(resumed)startSound();}
                else if(change<0)stopSound();
            },handler).build();
    }
    private void startSound(){if(started)return;Native.start(storage.getAbsolutePath());started=true;Native.event(5,0,getPreferences(0).getInt("master",2048));instrument.resume();}
    private void stopSound(){if(!started)return;instrument.pause();Native.stop();started=false;}
    @Override protected void onResume(){super.onResume();resumed=true;midi.resume();if(audio.requestAudioFocus(focus)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED)startSound();handler.post(update);}
    @Override protected void onPause(){resumed=false;handler.removeCallbacks(update);midi.pause();stopSound();audio.abandonAudioFocusRequest(focus);super.onPause();}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results){
        super.onRequestPermissionsResult(request,permissions,results);
        if(request==71){boolean granted=true;for(int result:results)granted&=result==android.content.pm.PackageManager.PERMISSION_GRANTED;if(granted)midi.scanBluetooth();else toast("Bluetooth permissions are needed to discover MIDI pads");}
    }
    private void toast(String message){Toast.makeText(this,message,Toast.LENGTH_LONG).show();}
    private void menu(){
        instrument.releaseAll();
        String[] items={"Connect USB / available MIDI","Connect Bluetooth LE MIDI","MIDI routing and pad notes","Disconnect MIDI","Master volume","Export Android backup","Restore Android backup","Import user sample slot","Audio diagnostics","MIDI knobs → FM-1"};
        new AlertDialog.Builder(this).setTitle("SLOOP").setItems(items,(d,n)->{
            switch(n){
            case 0:midi.chooseUsb();break;
            case 1:midi.scanBluetooth();break;
            case 2:midi.routing();break;
            case 3:midi.disconnect();break;
            case 4:volume();break;
            case 5:pick(81,true,"sloop-android.bin");break;
            case 6:pick(82,false,null);break;
            case 7:new AlertDialog.Builder(this).setTitle("Import a SLOOP FSMP sample image").setItems(new String[]{"USR1","USR2","USR3","USR4"},(x,slot)->{sampleSlot=slot;pick(83,false,null);}).show();break;
            case 8:new AlertDialog.Builder(this).setMessage(Native.status()+"\nEngine: 44.1 kHz. Output rate conversion: linear interpolation. Playback stops when the app leaves the foreground.").setPositiveButton("OK",null).show();break;
            case 9:midi.encoderSettings();break;
            }
        }).show();
    }
    private void volume(){SeekBar seek=new SeekBar(this);seek.setMax(4096);seek.setProgress(getPreferences(0).getInt("master",2048));seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
        public void onProgressChanged(SeekBar s,int value,boolean user){Native.event(5,0,value);getPreferences(0).edit().putInt("master",value).apply();}
        public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}
    });new AlertDialog.Builder(this).setTitle("Master volume").setView(seek).setPositiveButton("OK",null).show();}
    private void pick(int request,boolean create,String name){
        Intent intent=new Intent(create?Intent.ACTION_CREATE_DOCUMENT:Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream");
        if(name!=null)intent.putExtra(Intent.EXTRA_TITLE,name);startActivityForResult(intent,request);
    }
    private byte[] readLimited(InputStream input,int maximum)throws IOException{
        if(input==null)throw new IOException("Could not open file");
        try(InputStream in=input;ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1){if(out.size()+n>maximum)throw new IOException("File is too large");out.write(b,0,n);}return out.toByteArray();
        }
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(result!=RESULT_OK||data==null||data.getData()==null)return;
        try{
            if(request==81){stopSound();try(OutputStream out=getContentResolver().openOutputStream(data.getData(),"wt")){if(out==null)throw new IOException("Could not create file");Files.copy(storage.toPath(),out);}toast("Android backup exported");}
            else if(request==82){byte[] image=readLimited(getContentResolver().openInputStream(data.getData()),1048576);if(image.length!=1048576)throw new IOException("Expected a 1 MiB Android backup");
                stopSound();if(!Native.restore(image))throw new IOException("Could not restore backup");
                toast("Backup restored");}
            else if(request==83){byte[] sample=readLimited(getContentResolver().openInputStream(data.getData()),81920);if(resumed&&!started)startSound();Native.sample(sampleSlot,sample);toast("Sample queued; check audio diagnostics for result");}
        }catch(IOException|RuntimeException e){toast(e.getMessage());}
        if(resumed&&!started)startSound();
    }
}
