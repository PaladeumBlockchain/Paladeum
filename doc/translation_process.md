Translations
============

The desktop wallet of Paladeum Core can be shown in several languages. The translation files are
kept in this repository, in `src/qt/locale/`.

### Helping to translate
Edit the `.ts` file of your language in `src/qt/locale/`, for example with Qt Linguist (included
in the Qt SDK), and send the change as a pull request to
[the repository](https://github.com/PaladeumBlockchain/Paladeum).

### Writing code with translations
We use automated scripts to help extract translations in both Qt, and non-Qt source files. It is rarely necessary to manually edit the files in `src/qt/locale/`. The translation source files must adhere to the following format:
`paladeum_xx_YY.ts or paladeum_xx.ts`

`src/qt/locale/paladeum_en.ts` is treated in a special way. It is used as the source for all other translations. Whenever a string in the source code is changed, this file must be updated to reflect those changes. A custom script is used to extract strings from the non-Qt parts. This script makes use of `gettext`, so make sure that utility is installed (ie, `apt-get install gettext` on Ubuntu/Debian). Once this has been updated, `lupdate` (included in the Qt SDK) is used to update `paladeum_en.ts`.

To automatically regenerate the `paladeum_en.ts` file, run the following commands:
```sh
cd src/
make translate
```

`src/qt/paladeum_locale.qrc` takes care of generating `.qm` (binary compiled) files from `.ts` (source files) files. It’s mostly automated, and you shouldn’t need to worry about it.

**Example Qt translation**
```cpp
QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
```

### Creating a pull-request
A pull request that changes code should not include updates to the translation source files.
They are updated before releases, which also avoids merge conflicts in these files.

### Handling Plurals (in source files)
When new plurals are added to the source file, it's important to do the following steps:

1. Open `paladeum_en.ts` in Qt Linguist (included in the Qt SDK)
2. Search for `%n`, which will take you to the parts in the translation that use plurals
3. Look for empty `English Translation (Singular)` and `English Translation (Plural)` fields
4. Add the appropriate strings for the singular and plural form of the base string
5. Mark the item as done (via the green arrow symbol in the toolbar)
6. Repeat from step 2, until all singular and plural forms are in the source file
7. Save the source file

### Translating a new language
To create a new language template, you will need to edit the languages manifest file `src/qt/paladeum_locale.qrc` and add a new entry. Below is an example of the English language entry.

```xml
<qresource prefix="/translations">
    <file alias="en">locale/paladeum_en.qm</file>
    ...
</qresource>
```

**Note:** that the language translation file **must end in `.qm`** (the compiled extension), and not `.ts`.

Add the new `.ts` file to `src/Makefile.qt.include` as well, in the list of the other
translation files.
