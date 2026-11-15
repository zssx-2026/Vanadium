package inv.report;

/** Expected, user-facing failure: printed as "[error] message" with exit code 1. */
public final class AppException extends RuntimeException {

  private static final long serialVersionUID = 1L;

  public AppException(String message) {
    super(message);
  }

  public AppException(String message, Throwable cause) {
    super(message, cause);
  }
}
