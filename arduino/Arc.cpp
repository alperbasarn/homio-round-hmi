#include "Arc.h"

Arc::Arc(Arduino_GFX* graphics)
  : gfx(graphics), centerX(0), centerY(0), radius(0), arcWidth(0),
    maxArcLength(0), startAngle(0), currentPercentage(0), previousPercentage(0),
    targetPercentage(0), animationSpeed(5), arcColor(WHITE), backgroundColor(BLACK),
    segmentVisible(true), currentSegmentPercentage(100),
    prevSegmentPercentage(100), segmentColor(0x4208),  // Dark gray color for segment
    segmentAnimationActive(false), currentAnimStep(AnimationStep::NONE),
    savedSetpoint(0), animationCounter(0), animationStartPoint(0),
    animationEndPoint(100), arcAnimationStartTime(0), arcAnimationDuration(300),
    stepStartTime(0) {}

void Arc::initialize(int cx, int cy, int rad, int width, int length, int start) {
  centerX = cx;
  centerY = cy;
  radius = rad;
  arcWidth = width;
  maxArcLength = length;
  startAngle = start;
  currentPercentage = 0;
  previousPercentage = 0;
  targetPercentage = 0;
  segmentVisible = true;
  currentSegmentPercentage = 0;  // Start with no segment
  prevSegmentPercentage = 0;
  segmentAnimationActive = false;
  currentAnimStep = AnimationStep::NONE;
  savedSetpoint = 0;
  animationCounter = 0;
  animationStartPoint = 0;
  animationEndPoint = 100;
  arcAnimationStartTime = 0;

  // Draw the full segment (background arc/slot)
  drawFullSegment();
}

void Arc::update() {
  // If segment animation is active, handle animation sequence
  if (segmentAnimationActive) {
    updateAnimationStep();
    return;
  }

  // Normal arc animation
  unsigned long currentMillis = millis();
  
  // If target percentage changed, start animation
  if (currentPercentage != targetPercentage && arcAnimationStartTime == 0) {
    previousPercentage = currentPercentage;
    arcAnimationStartTime = currentMillis;
    // Store the animation start point
    arcAnimationStartPoint = currentPercentage;
  }
  
  // If animation is in progress
  if (arcAnimationStartTime > 0) {
    // Calculate animation progress
    float progress = constrain((float)(currentMillis - arcAnimationStartTime) / (float)arcAnimationDuration, 0.0f, 1.0f);
    
    // Update current percentage based on progress
    currentPercentage = arcAnimationStartPoint + (targetPercentage - arcAnimationStartPoint) * progress;
    
    // Draw the updated arc
    draw();
    
    // Check if animation is complete
    if (progress >= 1.0f) {
      currentPercentage = targetPercentage;
      arcAnimationStartTime = 0; // Reset animation start time
    }
  }
}

void Arc::draw() {
  // Only redraw if percentage has changed
  if (currentPercentage != previousPercentage) {
    // Calculate current and previous arc sweep angles, ensuring we don't exceed maxArcLength
    float currentArcLength = constrain((maxArcLength * currentPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);
    float previousArcLength = constrain((maxArcLength * previousPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);
    
    // Calculate arc end points
    float currentEndAngle = startAngle + currentArcLength;
    float previousEndAngle = startAngle + previousArcLength;
    
    if (currentPercentage > previousPercentage) {
      // Growing the arc - draw only the new part
      if (startAngle + previousArcLength != startAngle + currentArcLength) {
        gfx->fillArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                    startAngle + previousArcLength, startAngle + currentArcLength, arcColor);
      }
    } 
    else if (currentPercentage < previousPercentage) {
      // Shrinking the arc - erase only the part that should be hidden
      if (startAngle + currentArcLength != startAngle + previousArcLength) {
        gfx->fillArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                    startAngle + currentArcLength, startAngle + previousArcLength, segmentColor);
      }
    }
    
    // Update previous percentage
    previousPercentage = currentPercentage;
  }
}

void Arc::drawSegment(int startPercent, int endPercent) {
  // Ensure percentages are clamped properly
  startPercent = constrain(startPercent, 0, 100);
  endPercent = constrain(endPercent, 0, 100);
  
  // Calculate segment sweep angles, ensuring we don't exceed maxArcLength
  float startDeg = startAngle + constrain((maxArcLength * startPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);
  float endDeg = startAngle + constrain((maxArcLength * endPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);

  // Only draw if there's actually an arc to draw
  if (startDeg < endDeg) {
    // Draw the segment with the same dimensions as the arc
    gfx->fillArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                startDeg, endDeg, segmentColor);
  }
}

void Arc::drawFullSegment() {
  // Draw the full segment from start to end
  drawSegment(0, 100);
}

void Arc::clearSegment(int startPercent, int endPercent) {
  // Ensure percentages are clamped properly
  startPercent = constrain(startPercent, 0, 100);
  endPercent = constrain(endPercent, 0, 100);
  
  // Calculate segment sweep angles
  float startDeg = startAngle + constrain((maxArcLength * startPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);
  float endDeg = startAngle + constrain((maxArcLength * endPercent) / 100.0f, 0.0f, maxArcLength - 0.5f);

  // Clear the segment area with background color
  if (startDeg < endDeg) {
    gfx->fillArc(centerX, centerY, 
                radius + arcWidth/2 + 1,  // Slightly larger to ensure clean erasure
                radius - arcWidth/2 - 1,
                startDeg, endDeg, backgroundColor);
  }
}

void Arc::clearFullSegment() {
  // Clear the full segment with slightly wider area
  gfx->fillArc(centerX, centerY, 
               radius + arcWidth/2 + 2,  // Slightly larger to ensure clean erasure
               radius - arcWidth/2 - 2,
               startAngle - 2, startAngle + maxArcLength + 2, backgroundColor);
}

void Arc::clearArc() {
  // Clear the arc area (not segment walls) and draw segment color
  gfx->fillArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
               startAngle, startAngle + maxArcLength, segmentColor);
}

void Arc::setColor(uint16_t color) {
  arcColor = color;
  // Automatically set segment color to a darker shade
  segmentColor = (arcColor & 0xF7DE) >> 1;  // Roughly half brightness of arc color
  
  // If we have a visible arc, redraw it with the new color
  if (currentPercentage > 0) {
    // Redraw the arc with the new color
    gfx->fillArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                startAngle, startAngle + constrain((maxArcLength * currentPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f), arcColor);
  }
}

void Arc::setPercentage(int percentage) {
  // Clamp percentage to 0-100 range
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;

  // If not in animation mode, set target percentage as normal
  if (!segmentAnimationActive) {
    // Only start a new animation if target is different
    if (targetPercentage != percentage) {
      targetPercentage = percentage;
      // Reset animation start time to trigger animation in update()
      arcAnimationStartTime = 0;
    }
  } else {
    // Save the setpoint for later use when animation stops
    savedSetpoint = percentage;
  }
}

void Arc::reset() {
  // Reset arc and segment to initial state
  currentPercentage = 0;
  previousPercentage = 0;
  targetPercentage = 0;
  arcAnimationStartTime = 0;

  // Clear everything
  clearFullSegment();

  // Reset animation flags
  segmentAnimationActive = false;
  
  // Start with no segment
  currentSegmentPercentage = 0;
  prevSegmentPercentage = 0;
  
  // Initialize animation variables
  unsigned long startTime = millis();
  unsigned long animDuration = 500; // 500ms for reset animation
  int lastSegmentEnd = 0;
  
  // Animate segment drawing in increments rather than redrawing everything
  while (true) {
    unsigned long currentTime = millis();
    float progress = constrain((float)(currentTime - startTime) / (float)animDuration, 0.0f, 1.0f);
    
    // Calculate current segment end point
    int segmentEnd = progress * 100;
    
    // Only draw the newly visible part of the segment
    if (segmentEnd > lastSegmentEnd) {
      drawSegment(lastSegmentEnd, segmentEnd);
      lastSegmentEnd = segmentEnd;
    }
    
    // Once animation is complete, break the loop
    if (progress >= 1.0f) break;
    
    // Small delay for visible animation
    delay(10);
  }

  // Ensure the segment is fully drawn as a final step
  drawFullSegment();
}

void Arc::startSegmentAnimation() {
  if (!segmentAnimationActive) {
    segmentAnimationActive = true;
    savedSetpoint = targetPercentage;  // Save current setpoint

    // Clear the arc (don't show it during animation)
    clearArc();

    // Start with shrinking arc to 0 if it's already drawn
    currentAnimStep = AnimationStep::SHRINK_ARC_TO_ZERO;
    currentPercentage = 0;
    previousPercentage = 0;

    // Initial segment state is fully drawn
    currentSegmentPercentage = 100;
    prevSegmentPercentage = 100;

    animationCounter = 0;
    animationStartPoint = 0;
    animationEndPoint = 100;
    
    // Reset animation timing
    stepStartTime = millis();
  }
}

void Arc::stopSegmentAnimation() {
  if (segmentAnimationActive) {
    // Set flag to stop animation after current step completes
    animationCounter = -1;  // Special value indicating we want to stop
    
    // If we're already past GROW_SEGMENT_START_TO_END, move directly to final step
    if (static_cast<int>(currentAnimStep) > static_cast<int>(AnimationStep::GROW_SEGMENT_START_TO_END)) {
      currentAnimStep = AnimationStep::GROW_ARC_TO_SETPOINT;
      stepStartTime = 0; // Reset timing for next step
    }
    // Otherwise, we'll let the current step finish and then handle the transition
  }
}

void Arc::updateAnimationStep() {
  // Time-based animation variables
  unsigned long currentMillis = millis();
  float animationProgress = 0.0f;
  const unsigned long stepDuration = 300; // Duration for each animation step
  
  // First time in a new step, initialize timing
  if (currentAnimStep != AnimationStep::NONE && stepStartTime == 0) {
    stepStartTime = currentMillis;
  }
  
  // Calculate animation progress for the current step
  if (stepStartTime > 0) {
    animationProgress = constrain((float)(currentMillis - stepStartTime) / (float)stepDuration, 0.0f, 1.0f);
  }
  
  // Handle animation based on current step
  switch (currentAnimStep) {
    case AnimationStep::SHRINK_ARC_TO_ZERO:
      // Skip this since we already cleared the arc in startSegmentAnimation
      currentAnimStep = AnimationStep::SHRINK_SEGMENT_TO_START;
      stepStartTime = 0; // Reset for next step
      break;

    case AnimationStep::SHRINK_SEGMENT_TO_START: {
      // Save previous segment value
      prevSegmentPercentage = currentSegmentPercentage;
      
      // Calculate current position based on animation progress
      currentSegmentPercentage = 100 * (1.0f - animationProgress);
      
      // Only update the portion that changed
      if (prevSegmentPercentage > currentSegmentPercentage) {
        // Clear the segment part that's being removed
        clearSegment(currentSegmentPercentage, prevSegmentPercentage);
      }

      // If this step is complete, move to next
      if (animationProgress >= 1.0f) {
        // Ensure we're fully cleared
        currentSegmentPercentage = 0;
        clearFullSegment(); // Clear any remnants to be safe
        
        currentAnimStep = AnimationStep::GROW_SEGMENT_START_TO_END;
        stepStartTime = 0; // Reset for next step
      }
      break;
    }

    case AnimationStep::GROW_SEGMENT_START_TO_END: {
      // Store previous segment position
      prevSegmentPercentage = currentSegmentPercentage;
      
      // Calculate current position based on animation progress
      currentSegmentPercentage = 100 * animationProgress;
      
      // Check if this is the final animation step (we're stopping)
      if (animationCounter == -1) {
        // Simultaneously grow the segment and the arc
        
        // Update segment
        if (currentSegmentPercentage > prevSegmentPercentage) {
          drawSegment(prevSegmentPercentage, currentSegmentPercentage);
        }
        
        // Update arc - grow proportionally with the segment
        previousPercentage = currentPercentage;
        currentPercentage = savedSetpoint * animationProgress;
        
        // Only draw if percentage changed
        if (currentPercentage != previousPercentage) {
          float currentArcLength = constrain((maxArcLength * currentPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);
          float previousArcLength = constrain((maxArcLength * previousPercentage) / 100.0f, 0.0f, maxArcLength - 0.5f);
          
          if (startAngle + previousArcLength != startAngle + currentArcLength) {
            gfx->fillArc(centerX, centerY, radius + arcWidth/2, radius - arcWidth/2,
                        startAngle + previousArcLength, startAngle + currentArcLength, arcColor);
          }
        }
      } else {
        // Normal segment growth
        if (currentSegmentPercentage > prevSegmentPercentage) {
          drawSegment(prevSegmentPercentage, currentSegmentPercentage);
        }
      }

      // If step is complete, move to next
      if (animationProgress >= 1.0f) {
        // Check if we're stopping animation
        if (animationCounter == -1) {
          // End animation - both segment and arc are now at their final positions
          segmentAnimationActive = false;
          currentAnimStep = AnimationStep::NONE;
        } else {
          currentAnimStep = AnimationStep::SHRINK_SEGMENT_START_TO_END;
        }
        stepStartTime = 0; // Reset for next step
      }
      break;
    }

    case AnimationStep::SHRINK_SEGMENT_START_TO_END: {
      // Store previous segment position
      prevSegmentPercentage = currentSegmentPercentage;
      
      // Calculate current position based on animation progress
      currentSegmentPercentage = 100 * (1.0f - animationProgress);
      
      // Only update the portion that changed
      if (prevSegmentPercentage > currentSegmentPercentage) {
        // Clear the segment part that's being removed
        clearSegment(currentSegmentPercentage, prevSegmentPercentage);
      }

      // If step is complete, move to next
      if (animationProgress >= 1.0f) {
        // Ensure we're fully cleared
        currentSegmentPercentage = 0;
        clearFullSegment(); // Clear any remnants to be safe
        
        currentAnimStep = AnimationStep::GROW_SEGMENT_END_TO_START;
        currentSegmentPercentage = 0; // Reset for the next step
        stepStartTime = 0; // Reset for next step
      }
      break;
    }

    case AnimationStep::GROW_SEGMENT_END_TO_START: {
      // Store previous segment position
      prevSegmentPercentage = currentSegmentPercentage;
      
      // Calculate current position based on animation progress
      currentSegmentPercentage = 100 * animationProgress;
      
      // Draw segment from end to start with proper caps
      if (currentSegmentPercentage > prevSegmentPercentage) {
        int prevStart = 100 - prevSegmentPercentage;
        int currentStart = 100 - currentSegmentPercentage;
        
        // Only draw the portion that changed
        drawSegment(currentStart, prevStart);
      }

      // If step is complete, move to next
      if (animationProgress >= 1.0f) {
        currentAnimStep = AnimationStep::SHRINK_SEGMENT_END_TO_START;
        stepStartTime = 0; // Reset for next step
      }
      break;
    }

    case AnimationStep::SHRINK_SEGMENT_END_TO_START: {
      // Store previous segment position
      prevSegmentPercentage = currentSegmentPercentage;
      
      // Calculate current position based on animation progress
      currentSegmentPercentage = 100 * (1.0f - animationProgress);
      
      // If the segment was reduced, clear the part that's no longer covered
      if (prevSegmentPercentage > currentSegmentPercentage) {
        int prevStart = 100 - prevSegmentPercentage;
        int currentStart = 100 - currentSegmentPercentage;
        
        // Only clear the portion that changed
        clearSegment(prevStart, currentStart);
      }

      // If step is complete, decide on next step
      if (animationProgress >= 1.0f) {
        // Ensure complete clearing of any remaining segment
        currentSegmentPercentage = 0;
        clearFullSegment(); // Clear any remnants to be safe
        
        // Go to next step based on animation counter
        if (animationCounter == -1) {
          currentAnimStep = AnimationStep::GROW_ARC_TO_SETPOINT;
        } else {
          currentAnimStep = AnimationStep::GROW_SEGMENT_START_TO_END;
          currentSegmentPercentage = 0; // Reset for the next step
          animationCounter++; // Increment counter for the next cycle
        }
        stepStartTime = 0; // Reset for next step
      }
      break;
    }

    case AnimationStep::GROW_ARC_TO_SETPOINT: {
      // This step is now only used when coming from animation steps other than GROW_SEGMENT_START_TO_END
      // Draw the full segment background first
      drawFullSegment();

      // Set up arc animation to the saved setpoint
      previousPercentage = 0;
      currentPercentage = 0;
      targetPercentage = savedSetpoint;
      
      // Start a new arc animation
      arcAnimationStartTime = currentMillis;
      arcAnimationStartPoint = 0;

      // End animation mode
      segmentAnimationActive = false;
      currentAnimStep = AnimationStep::NONE;
      stepStartTime = 0;
      break;
    }

    case AnimationStep::NONE:
    default:
      // Do nothing
      break;
  }
}