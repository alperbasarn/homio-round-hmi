#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QNOB Controller Application
Author: Alper Basaran
"""

import tkinter as tk
from sound_controller_app import QNOBApp

def main():
    # Initialize the root window
    root = tk.Tk()
    root.title("QNOB Controller")
    
    # Create the application
    app = QNOBApp(root)
    
    # Set up window close handler
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    
    # Start the main event loop
    root.mainloop()

if __name__ == "__main__":
    main()