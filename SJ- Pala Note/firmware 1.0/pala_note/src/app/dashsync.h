#pragma once

// Upload enriched notes to the Bud-E Mac dashboard during Sync.
// Requires WiFi to already be connected. Each successfully uploaded note gets a
// /notes/note_NNN.up marker so it isn't re-uploaded on the next Sync.
// Returns the number of notes newly uploaded.
int syncNotesToDashboard();

// Apply the dashboard's retention decisions: remove local copies of notes that
// were moved to dashboard-only or auto-migrated (older than the policy window).
// Returns the number removed locally. WiFi must be connected.
int reconcileWithDashboard();
