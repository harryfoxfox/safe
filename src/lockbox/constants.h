/*
  Lockbox: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef _lockbox_constants_h
#define _lockbox_constants_h

#include <lockbox/product_name.h>

#define LOCKBOX_APP_STARTED_COOKIE_FILENAME "AppStarted"
#define LOCKBOX_RECENTLY_USED_PATHS_V1_FILE_NAME "RecentlyUsedPathsV1.db"

enum {
  LOCKBOX_RECENTLY_USED_PATHS_MENU_NUM_ITEMS=10,
};

#define LOCKBOX_SOURCE_CODE_WEBSITE "http://github.com/rianhunter/safe"

/* UI Strings follow */

#define LOCKBOX_TRAY_ICON_TOOLTIP PRODUCT_NAME_A

#define LOCKBOX_PROGRESS_CREATING_TITLE ("Creating New " ENCRYPTED_STORAGE_NAME_A "...")
#define LOCKBOX_PROGRESS_CREATING_MESSAGE ("Creating new " ENCRYPTED_STORAGE_NAME_A "...")

#define LOCKBOX_PROGRESS_MOUNTING_TITLE ("Mounting New " ENCRYPTED_STORAGE_NAME_A "...")
#define LOCKBOX_PROGRESS_MOUNTING_MESSAGE ("Mounting new " ENCRYPTED_STORAGE_NAME_A "...")

#define LOCKBOX_PROGRESS_MOUNTING_EXISTING_TITLE ("Mounting Existing " ENCRYPTED_STORAGE_NAME_A "...")
#define LOCKBOX_PROGRESS_MOUNTING_EXISTING_MESSAGE ("Mounting existing " ENCRYPTED_STORAGE_NAME_A "...")

#define LOCKBOX_PROGRESS_VERIFYING_PASS_TITLE ("Verifying " ENCRYPTED_STORAGE_NAME_A " Password...")
#define LOCKBOX_PROGRESS_VERIFYING_PASS_MESSAGE ("Verifying " ENCRYPTED_STORAGE_NAME_A " password...")

#define LOCKBOX_PROGRESS_READING_CONFIG_TITLE ("Reading " ENCRYPTED_STORAGE_NAME_A " Configuration...")
#define LOCKBOX_PROGRESS_READING_CONFIG_MESSAGE ("Reading " ENCRYPTED_STORAGE_NAME_A " configuration...")

#define LOCKBOX_DIALOG_PASS_INCORRECT_TITLE "Password is Incorrect"
#define LOCKBOX_DIALOG_PASS_INCORRECT_MESSAGE "The password you have entered is incorrect."

#define LOCKBOX_DIALOG_NO_CONFIG_EXISTS_TITLE ("Not a " ENCRYPTED_STORAGE_NAME_A)
#define LOCKBOX_DIALOG_NO_CONFIG_EXISTS_MESSAGE ("The location you selected is not a " ENCRYPTED_STORAGE_NAME_A ".")

#define LOCKBOX_DIALOG_QUIT_CONFIRMATION_TITLE ("Quit " PRODUCT_NAME_A " Now?")
#define LOCKBOX_DIALOG_QUIT_CONFIRMATION_MESSAGE ("You currently have " ENCRYPTED_STORAGE_NAME_A " s mounted, if you quit they will not be accessible until you run " PRODUCT_NAME_A " again. Quit " PRODUCT_NAME_A " Now?")

#define LOCKBOX_DIALOG_UNKNOWN_CREATE_ERROR_TITLE "Unknown Error"
#define LOCKBOX_DIALOG_UNKNOWN_CREATE_ERROR_MESSAGE ("Unknown error occurred while creating new " ENCRYPTED_STORAGE_NAME_A ".")

#define LOCKBOX_DIALOG_UNKNOWN_MOUNT_ERROR_TITLE "Unknown Error"
#define LOCKBOX_DIALOG_UNKNOWN_MOUNT_ERROR_MESSAGE ("Unknown error occurred while mounting existing " ENCRYPTED_STORAGE_NAME_A ".")

#define LOCKBOX_DIALOG_ABOUT_TITLE ("About " PRODUCT_NAME_A)
#define LOCKBOX_DIALOG_WELCOME_TITLE ("Welcome to " PRODUCT_NAME_A "!")

#define LOCKBOX_TRAY_ICON_WELCOME_TITLE (PRODUCT_NAME_A " is now Running!")
#define LOCKBOX_TRAY_ICON_WELCOME_MSG                                   \
  (                                                                     \
   "If you need to use "                                                \
   PRODUCT_NAME_A                                                       \
   ", just right-click on this icon."                                   \
                                                      )
#define LOCKBOX_TRAY_ICON_MAC_WELCOME_MSG ("If you need to use " PRODUCT_NAME_A ", just click on the " PRODUCT_NAME_A " menu bar icon.")

#define LOCKBOX_DIALOG_WELCOME_TEXT \
    (PRODUCT_NAME_A " is now running. You can use it by clicking " \
     "the " PRODUCT_NAME_A " icon in the tray. Get started by clicking " \
     "one of the actions below:")
#define LOCKBOX_DIALOG_WELCOME_CREATE_BUTTON \
  ("Create New " ENCRYPTED_STORAGE_NAME_A "...")
#define LOCKBOX_DIALOG_WELCOME_MOUNT_BUTTON \
  ("Mount Existing " ENCRYPTED_STORAGE_NAME_A "...")

#define LOCKBOX_NOTIFICATION_TEST_TITLE "Short Title"
#define LOCKBOX_NOTIFICATION_TEST_MESSAGE \
  ("Very long message full of meaningful info that you " \
   "will find very interesting because you love to read " \
   "tray icon bubbles. Don't you? Don't you?!?!")

#define LOCKBOX_DIALOG_ABOUT_TAGLINE "Protect your files."
#define LOCKBOX_DIALOG_ABOUT_VERSION "Version 1.0"
#define LOCKBOX_DIALOG_ABOUT_BYLINE "Safe is Free Software"

#endif
