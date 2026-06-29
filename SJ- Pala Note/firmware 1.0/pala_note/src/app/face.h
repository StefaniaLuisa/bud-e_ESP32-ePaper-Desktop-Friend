#pragma once

// Bud-E's face — EMO-style rounded eyes, drawn white on a dark screen.
// Expressions "snap" to a new mood on events (e-paper can't animate smoothly).

// Expressions follow the FluxGarage RoboEyes style (rounded-rect eyes with
// triangular/curved eyelid overlays). On e-paper they "snap" to a new mood on
// events rather than tweening smoothly.
enum BudeMood {
  MOOD_AWAKE,      // default — two open rounded-square eyes
  MOOD_BLINK,      // eyes closed (thin bars)
  MOOD_HAPPY,      // smiling/squinting eyes (upward crescents)
  MOOD_SLEEPY,     // low half-closed slits (before dozing off)
  MOOD_LISTENING,  // big alert eyes (when you hold REC to talk)
  MOOD_THINKING,   // eyes glance up (while it's working)
  MOOD_LOVE,       // hearts (celebrations, e.g. finishing a pomodoro)
  MOOD_TIRED,      // RoboEyes "tired" — outer top corners droop
  MOOD_ANGRY       // RoboEyes "angry" — inner top corners droop
};

// Draw just the eyes for the given mood onto the current buffer.
// Assumes the background is already cleared; does NOT refresh the screen,
// so you can add a name, clock, etc. before refreshing yourself.
void drawBudeEyes(BudeMood mood);

// Convenience: clear to the background, draw the eyes, and push to e-paper.
void drawBudeFace(BudeMood mood);
