"""Execute the production Java install task with a synchronous fake JNI engine."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = 'com/openport/galaxyandroid/'
SOURCES = {
    'android/net/Uri.java': '''package android.net;
public class Uri {}''',
    'android/content/ContentResolver.java': '''package android.content;
import java.io.*;
public class ContentResolver {
 public InputStream openInputStream(android.net.Uri uri) { return new ByteArrayInputStream(new byte[]{1,2,3}); }
}''',
    'android/app/Activity.java': '''package android.app;
import java.io.*;
public class Activity {
 public final File root;
 public Activity(File root) { this.root=root; }
 public File getFilesDir() { File f=new File(root,"private"); f.mkdirs(); return f; }
 public File getExternalFilesDir(String ignored) { File f=new File(root,"external"); f.mkdirs(); return f; }
 public android.content.ContentResolver getContentResolver() { return new android.content.ContentResolver(); }
}''',
    'android/content/Context.java': '''package android.content;
import java.io.File;
public abstract class Context {
 public abstract File getExternalFilesDir(String type);
 public abstract File getFilesDir();
}''',
    'android/os/Handler.java': '''package android.os;
public class Handler { public boolean post(Runnable task) { task.run(); return true; } }''',
    'android/os/Environment.java': '''package android.os;
import java.io.File;
public class Environment {
 public static final String DIRECTORY_DOWNLOADS="Download";
 public static File getExternalStorageDirectory() { return new File("/nonexistent"); }
 public static File getExternalStoragePublicDirectory(String type) { return new File("/nonexistent"); }
}''',
    'android/util/Log.java': '''package android.util;
public class Log { public static int e(String tag,String message,Throwable error) { return 0; } }''',
    PACKAGE + 'CookerEngine.java': '''package com.openport.galaxyandroid;
import android.app.Activity;
import java.io.*;
public class CookerEngine {
 static String mode;
 static int conversions;
 public interface CookerCallback {
  void onLog(String line); void onProgress(int percent,String message); void onFinished(boolean ok,String message);
 }
 static void write(File target,byte[] bytes) throws IOException {
  target.getParentFile().mkdirs();
  try(FileOutputStream out=new FileOutputStream(target)) { out.write(bytes); }
 }
 public static int extractDisc(String input,String output,boolean movies,CookerCallback cb) {
  if(mode.equals("extract_error")) { cb.onLog("[ERROR] Synthetic extraction failure"); return 1; }
  try { write(new File(output,"sys/fst.bin"),new byte[]{1}); }
  catch(IOException e) { throw new RuntimeException(e); }
  return 0;
 }
 public static boolean extractWithNodtool(Activity a,String in,String out,boolean movies,CookerCallback cb) { return false; }
 public static void prepareAssets(Activity a,CookerCallback cb) throws IOException {
  if(mode.equals("prepare_error")) throw new IOException("Synthetic preparation failure");
 }
 public static int runCooker(String home,String scripts,String input,String output,boolean movies,CookerCallback cb) {
  conversions++;
  if(mode.equals("linkage_error")) throw new UnsatisfiedLinkError("Synthetic JNI failure");
  try {
   write(new File(output,"sys/fst.bin"),new byte[]{1});
   if(!mode.equals("incomplete")) write(new File(output,"files/ObjectData/Coin.arc"),new byte[]{'C','R','A','R'});
  } catch(IOException e) { throw new RuntimeException(e); }
  if(mode.equals("conversion_error")) {
   cb.onLog("[Cook Warning] wait.bas: Synthetic conversion failure");
   cb.onLog("[ERROR] cook.convert returned non-zero code 1");
   cb.onLog("RuntimeError: cook.convert returned non-zero code 1");
   return 1;
  }
  return 0; // Deliberately does not call onFinished, matching the real JNI.
 }
 public static void deleteRecursive(File target) {
  if(target==null || !target.exists()) return;
  File[] children=target.listFiles();
  if(children!=null) for(File child:children) deleteRecursive(child);
  if(!target.delete()) throw new RuntimeException("Cannot delete temporary fixture");
 }
}''',
    PACKAGE + 'InstallTaskProbe.java': '''package com.openport.galaxyandroid;
import android.app.Activity;
import android.net.Uri;
import android.os.Handler;
import java.io.*;
import java.nio.file.Files;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
public class InstallTaskProbe {
 static void require(boolean condition,String detail) { if(!condition) throw new AssertionError(detail); }
 public static void main(String[] args) throws Exception {
  CookerEngine.mode=args[0]; CookerEngine.conversions=0;
  File root=Files.createTempDirectory("install-task").toFile();
  try {
   Activity activity=new Activity(root);
   File original=new File(root,"original.iso");
   CookerEngine.write(original,new byte[]{42});
   CountDownLatch done=new CountDownLatch(1);
   AtomicInteger calls=new AtomicInteger(); AtomicBoolean outcome=new AtomicBoolean();
   AtomicReference<String> message=new AtomicReference<>();
   boolean uri=args[0].equals("uri");
   CookerTask.start(activity,new Handler(),uri?"content://rom":original.getPath(),uri?new Uri():null,
     "Super Mario Galaxy.iso",true,new CookerTask.CookerTaskListener() {
      public void onLog(String line) {}
      public void onProgress(int percent,String value) {}
      public void onCompleted(boolean success,String detail) {
       calls.incrementAndGet(); outcome.set(success); message.set(detail); done.countDown();
      }
     });
   require(done.await(10,TimeUnit.SECONDS),"JNI return did not complete the install");
   require(calls.get()==1,"completion must be delivered exactly once");
   boolean success=args[0].equals("ok") || uri;
   require(outcome.get()==success,"wrong result: "+message.get());
   File output=new File(activity.getExternalFilesDir(null),"GalaxyData");
   require(GameDataLocator.checkDataDir(output)==success,"partial installation accepted");
   require(new File(output,".installing").exists()!=success,"pending install state incorrect");
   require(!new File(activity.getFilesDir(),"temp_extracted").exists(),"extraction cache left behind");
   require(!new File(activity.getFilesDir(),"temp_disc.iso").exists(),"ROM cache left behind");
   require(Files.readAllBytes(original.toPath())[0]==42,"original ROM changed");
   if(args[0].equals("conversion_error")) require(message.get().contains("Synthetic conversion failure"),"lost native diagnostic");
   if(args[0].equals("extract_error") || args[0].equals("prepare_error")) require(CookerEngine.conversions==0,"converted after earlier failure");
   System.out.println("INSTALL TASK VERIFIED: "+args[0]);
  } finally { CookerEngine.deleteRecursive(root); }
 }
}''',
}


@unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'JDK required for Android install-task probe')
class AndroidInstallTaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        for relative, source in SOURCES.items():
            target = cls.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source)
        for name in ('CookerTask.java', 'GameDataLocator.java'):
            shutil.copyfile(ROOT / 'platform/mobile/src' / PACKAGE / name, cls.root / PACKAGE / name)
        command = ['javac', '-d', str(cls.root / 'classes')]
        command.extend(str(path) for path in cls.root.rglob('*.java'))
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            cls.tmp.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_task(self, mode):
        result = subprocess.run(['java', '-cp', str(self.root / 'classes'),
                                 'com.openport.galaxyandroid.InstallTaskProbe', mode],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('INSTALL TASK VERIFIED', result.stdout)

    def test_native_return_completes_without_callback(self):
        self.run_task('ok')

    def test_document_uri_is_copied_and_cleaned(self):
        self.run_task('uri')

    def test_converter_failure_retains_detail_and_blocks_partial_data(self):
        self.run_task('conversion_error')

    def test_incomplete_conversion_is_rejected(self):
        self.run_task('incomplete')

    def test_extraction_failure_does_not_convert(self):
        self.run_task('extract_error')

    def test_preparation_failure_cleans_up(self):
        self.run_task('prepare_error')

    def test_jni_linkage_failure_reports_completion(self):
        self.run_task('linkage_error')


if __name__ == '__main__':
    unittest.main()
