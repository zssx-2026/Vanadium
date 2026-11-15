package inv.report;

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

/**
 * Entry point: java -jar vanadium-report.jar <command> [args].
 *
 * <p>All output is UTF-8 regardless of the console code page, so the Rust shell and the
 * C++ kernel can re-render it. Expected failures print a single "[error] ..." line and exit
 * with code 1; unexpected failures never leak a raw stack trace unless INV_DEBUG is set.
 */
public final class Main {

  private Main() {}

  public static void main(String[] argv) {
    PrintStream out = new PrintStream(
        new FileOutputStream(FileDescriptor.out), true, StandardCharsets.UTF_8);
    System.setOut(out);
    int code;
    try {
      code = Cli.run(argv, out);
    } catch (AppException e) {
      out.println("[error] " + (e.getMessage() == null ? "unknown error" : e.getMessage()));
      code = 1;
    } catch (Throwable e) {
      out.println("[error] internal error: " + e);
      if (System.getenv("INV_DEBUG") != null) {
        e.printStackTrace(out);
      }
      code = 1;
    }
    out.flush();
    System.exit(code);
  }
}
