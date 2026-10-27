package com.ast.re4decomp.disc;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * A catalogue of verified original code slices, never of executable ARM code.
 */
public final class ReferenceCatalog {
  public static final class Entry {
    public final int disc;
    public final String path;
    public final long offset, size;
    public final String sha1;
    Entry(String[] s) {
      disc = Integer.parseInt(s[0]);
      path = s[1];
      offset = Long.parseLong(s[2]);
      size = Long.parseLong(s[3]);
      sha1 = s[4];
    }
  }
  public final List<Entry> entries;
  public ReferenceCatalog(InputStream source) throws IOException {
    List<Entry> result = new ArrayList<>();
    try (BufferedReader r = new BufferedReader(
             new InputStreamReader(source, StandardCharsets.UTF_8))) {
      String line;
      while ((line = r.readLine()) != null) {
        if (line.isEmpty() || line.startsWith("#"))
          continue;
        String[] s = line.split("\t");
        if (s.length != 5)
          throw new IOException("Invalid reference catalogue");
        result.add(new Entry(s));
      }
    }
    // references.tsv (debug build) 116 rows, references-eu.tsv (European release) 134
    if (result.isEmpty())
      throw new IOException("Incomplete reference catalogue");
    entries = Collections.unmodifiableList(result);
  }
}
