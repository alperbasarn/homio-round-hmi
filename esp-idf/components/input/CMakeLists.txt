# Input component (Phase 2, BLD-11): the unified InputEvent now, InputRouter in WP2.1.
# Public surface depends on the model only; the drivers stay private.
idf_component_register(
    SRCS "src/InputEvent.cpp"
    INCLUDE_DIRS "include"
    REQUIRES model
    PRIV_REQUIRES touch_panel knob_controller esp_timer
)
