package inv.report;

import java.util.List;

/** Immutable archive model shared by the report, audit and inspect commands. */
public final class Model {

  private Model() {}

  public record Branch(
      String id,
      String path,
      String name,
      String parent,
      long createdMs,
      long headSeq,
      boolean isNode,
      List<String> children,
      boolean system) {}

  public record Entry(
      String path,
      boolean dir,
      long size,
      String sha,
      long crc,
      long mtime,
      int mode) {}

  public record Snapshot(
      long seq,
      String branch,
      long timeMs,
      String message,
      String parentSha,
      String id,
      List<Entry> entries,
      boolean hasEntries) {}
}
