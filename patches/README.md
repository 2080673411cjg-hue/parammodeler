# Close-Zoom Experiment and Rollback

Development should normally stay inside ParamModeler. The qgis_3d changes
are an experimental exception. Further core edits require explicit discussion
and a backup before editing.

`qgis-close-zoom.applypatch` is a Codex/apply_patch-format patch, not a
git-apply diff. The experiment has now been REVERTED at the user's request.
The patch is an archive only and must not be reapplied without approval.

Source backups: `../backups/2026-09-17-before-qgis-close-zoom/`.
These were reconstructed after editing, with core reverse/forward round-trip
verification and SHA-256 checksums. No previous DLL backup is available.

From the plugin directory:

```text
python patches/restore_close_zoom_backup.py
```

This checks all backup and live source hashes without changing live sources.
Only after the user requests rollback, close QGIS and run with `--apply` in
an environment authorized to write the core files. Restore both core and
plugin sources, then rebuild and deploy qgis_3d and plugin_parammodeler.
Later edits cause a preflight failure instead of being overwritten. Use the
before/after differences for manual merging in that case. The seven-file
restoration is not atomic against disk failures; rerun checks after any error.
