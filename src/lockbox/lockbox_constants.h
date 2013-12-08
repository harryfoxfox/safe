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

#define LOCKBOX_SOURCE_CODE_WEBSITE "http://github.com/rianhunter/bitvault"

/* UI Strings follow */

#define LOCKBOX_TRAY_ICON_TOOLTIP PRODUCT_NAME_A

#define LOCKBOX_PROGRESS_CREATING_TITLE "Creating New Bitvault..."
#define LOCKBOX_PROGRESS_CREATING_MESSAGE "Creating new Bitvault..."

#define LOCKBOX_PROGRESS_MOUNTING_TITLE "Mounting New Bitvault..."
#define LOCKBOX_PROGRESS_MOUNTING_MESSAGE "Mounting new Bitvault..."

#define LOCKBOX_PROGRESS_MOUNTING_EXISTING_TITLE "Mounting Existing Bitvault..."
#define LOCKBOX_PROGRESS_MOUNTING_EXISTING_MESSAGE "Mounting existing Bitvault..."

#define LOCKBOX_PROGRESS_VERIFYING_PASS_TITLE "Verifying Bitvault Password..."
#define LOCKBOX_PROGRESS_VERIFYING_PASS_MESSAGE "Verifying Bitvault password..."

#define LOCKBOX_PROGRESS_READING_CONFIG_TITLE "Reading Bitvault Configuration..."
#define LOCKBOX_PROGRESS_READING_CONFIG_MESSAGE "Reading Bitvault configuration..."

#define LOCKBOX_DIALOG_PASS_INCORRECT_TITLE "Password is Incorrect"
#define LOCKBOX_DIALOG_PASS_INCORRECT_MESSAGE "The password you have entered is incorrect."

#define LOCKBOX_DIALOG_NO_CONFIG_EXISTS_TITLE "Not a Bitvault"
#define LOCKBOX_DIALOG_NO_CONFIG_EXISTS_MESSAGE "The location you selected is not a Bitvault."

#define LOCKBOX_DIALOG_QUIT_CONFIRMATION_TITLE "Quit Bitvault Now?"
#define LOCKBOX_DIALOG_QUIT_CONFIRMATION_MESSAGE "You currently have Bitvaults mounted, if you quit they will not be accessible until you run Bitvault again. Quit Bitvault Now?"

#define LOCKBOX_TRAY_ICON_WELCOME_TITLE (PRODUCT_NAME_A " is now Running!")
#define LOCKBOX_TRAY_ICON_WELCOME_MSG                                   \
  (                                                                     \
   "If you need to use "                                                \
   PRODUCT_NAME_A                                                       \
   ", just right-click on this icon."                                   \
                                                      )
#define LOCKBOX_TRAY_ICON_MAC_WELCOME_MSG "If you need to use Bitvault, just click on Bitvault menu bar icon."

#define LOCKBOX_ABOUT_BLURB (                                           \
  PRODUCT_NAME_A " is an application that makes it easy to encrypt your files. " \
  "Encryption prevents others from reading your " \
  "files without knowing your password." \
                                         \
  "\r\n\r\n"                             \
                                         \
  "You use " PRODUCT_NAME_A                                     \
  " in the exact same way you would use a normal folder. "              \
  "Behind the scenes it's automatically encrypting your files on your behalf."\
                                                                        \
  "\r\n\r\n"                                                            \
                                                                        \
  "Use it to store tax returns, receipts, passwords, or anything else you hold " \
  "the most private. It also works well with cloud-based storage services." \
                                                                        \
  "\r\n\r\n"                                                            \
                                                                        \
  PRODUCT_NAME_A " is entirely composed of free software. Not free "    \
  "in the \"free beer\" sense but free in the \"freedom\" sense. "      \
  "All the code that makes up "                                         \
  PRODUCT_NAME_A                                                        \
  " is available "                                                      \
  "for you to read, modify, and distribute at your will."               \
                                                                        \
  "\r\n\r\n"                                                            \
                                                                        \
  "Bitvault is distributed in the hope that it will be useful, "    \
  "but WITHOUT ANY WARRANTY; without even the implied warranty of "     \
  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. USE AT YOUR OWN RISK." \
                                                                        \
  "\r\n\r\n"                                                            \
                                                                        \
  "Thanks for using " PRODUCT_NAME_A                                    \
  "! Hopefully it makes your life just a little "                       \
  "bit safer."                                                          \
                                                                        )

#define LOCKBOX_NOTIFICATION_TEST_TITLE "Short Title"
#define LOCKBOX_NOTIFICATION_TEST_MESSAGE \
  ("Very long message full of meaningful info that you " \
   "will find very interesting because you love to read " \
   "tray icon bubbles. Don't you? Don't you?!?!")

#endif
