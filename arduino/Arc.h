#ifndef ARC_H
#define ARC_H

#include <Arduino_GFX_Library.h>

enum class AnimationStep {
  NONE,
  SHRINK_ARC_TO_ZERO,
  SHRINK_SEGMENT_TO_START,
  GROW_SEGMENT_START_TO_END,
  SHRINK_SEGMENT_START_TO_END,
  GROW_SEGMENT_END_TO_START,
  SHRINK_SEGMENT_END_TO_START,
  GROW_ARC_TO_SETPOINT
};

class Arc {
private:
  Arduino_GFX* gfx;
  int centerX, centerY;      // Center position
  int radius;                // Radius of the arc
  int arcWidth;              // Width of the arc
  int maxArcLength;          // Maximum arc length in degrees
  int startAngle;            // Starting angle in degrees
  int currentPercentage;     // Current percentage (0-100)
  int previousPercentage;    // Previous percentage to calculate what to redraw
  int targetPercentage;      // Target percentage for animation
  int animationSpeed;        // Speed of animation (percentage change per update)
  uint16_t arcColor;         // Color of the arc
  uint16_t backgroundColor;  // Background color

  // Segment properties (segment is the background arc/slot)
  bool segmentVisible;           // Whether segment is visible
  int currentSegmentPercentage;  // Current segment animation percentage (0-100)
  int prevSegmentPercentage;     // Previous segment percentage
  uint16_t segmentColor;         // Color for the segment (darker than arc color)

  // Animation properties
  bool segmentAnimationActive;    // Flag for segment animation
  AnimationStep currentAnimStep;  // Current animation step
  int savedSetpoint;              // Saved setpoint for animation reset
  int animationCounter;           // Counter for controlling animation cycles
  int animationStartPoint;        // Start point for animations
  int animationEndPoint;          // End point for animations
  
  // Time-based animation properties
  unsigned long arcAnimationStartTime;  // Start time for arc animation
  unsigned long arcAnimationDuration;   // Duration for arc animation in milliseconds
  int arcAnimationStartPoint;           // Starting percentage for animation
  unsigned long stepStartTime = 0;      // Start time for animation step

public:
  Arc(Arduino_GFX* graphics);

  // Initialize arc properties
  void initialize(int centerX, int centerY, int radius, int arcWidth, int maxArcLength, int startAngle = 120);

  // Update arc animation
  void update();

  // Update arc animation
  void updateAnimationStep();

  // Draw the arc
  void draw();
  
  // Draw a rounded end cap for the arc
  void drawRoundedEndCap(int percentPosition, uint16_t color);
  
  // Clear a rounded end cap
  void clearRoundedEndCap(int percentPosition);

  // Draw segment (background arc/slot) within specific range
  void drawSegment(int startPercent, int endPercent);
  
  // Draw rounded cap for segment
  void drawSegmentRoundedCap(int percentPosition);

  // Draw full segment from start to end
  void drawFullSegment();

  // Clear segment within specific range
  void clearSegment(int startPercent, int endPercent);
  
  // Clear rounded cap for segment
  void clearSegmentRoundedCap(int percentPosition);

  // Clear full segment
  void clearFullSegment();

  // Clear just the arc (not the segment walls)
  void clearArc();

  // Set the arc color
  void setColor(uint16_t color);

  // Set the arc percentage (0-100)
  void setPercentage(int percentage);

  // Reset the arc and segment
  void reset();

  // Start segment animation
  void startSegmentAnimation();

  // Stop segment animation
  void stopSegmentAnimation();

  // Check if segment animation is active
  bool isSegmentAnimationActive() const {
    return segmentAnimationActive;
  }

  // Get current percentage
  int getPercentage() const {
    return currentPercentage;
  }
};

#endif  // ARC_H