# The README demo. Regenerate from the repository root with
#   make release && nix shell nixpkgs#vhs -c vhs docs/demo.tape
# It types into a real shell in a scratch workspace holding this repository's
# docs and links to the models in models/ (run `librarian setup` there first).

Output docs/demo.gif

Set Shell bash
Set FontSize 16
Set Width 1200
Set Height 700
Set Padding 20
Set Theme "Dracula"
Set TypingSpeed 45ms
Set PlaybackSpeed 1.0

Hide
Type "export PATH=$PWD/bin:$PATH COLORTERM=truecolor LANG=C.UTF-8 LC_ALL=C.UTF-8 PS1='$ '"
Enter
Type "cd $(mktemp -d) && mkdir data models notes && ln -s $OLDPWD/models/*.gguf models/ && cp $OLDPWD/README.md $OLDPWD/docs/*.md notes/ && clear"
Enter
Sleep 1s
Show

Type "librarian chat"
Sleep 400ms
Enter
# Wait for the models to load, or the keys echo as type-ahead.
Sleep 4s

# Tab completes /commands, then paths after /ingest.
Type "/ing"
Tab
Sleep 400ms
Type "no"
Tab
Sleep 600ms
Enter
Sleep 7s

Type "What is the minimum Windows version?"
Sleep 500ms
Enter
Sleep 7s

Type "/exit"
Sleep 400ms
Enter
Sleep 1s
