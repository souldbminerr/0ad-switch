---

name: "Release Process Task"
about: "This is a special issue template for planning releases. DO NOT USE it for normal issue reports."
title: "[RELEASE PROCESS] Release XX"
labels:

- "Type/Task"
- "Theme/Build & Packages"

---

*Please fill out relevant information in the next line, set Milestone to the relevant Release, then delete this line.*

# Release XX Process

This task tracks the progress of the next release. **Please do not report issues with the nightly build or with release candidates here:** instead create a new issue and set its Milestone to the relevant release.

All details about each step are documented in [ReleaseProcess](wiki/ReleaseProcess).

After performing steps, post a comment on the task, so that it appears in the activity feed and notifies followers.

**Release Manager:** @
**Translations Officer:** @

## Outstanding Issues

**All the following issues must be fixed in `main` (which closes the issue) and then cherry-picked to the release branch (after that, you can tick the checkbox below).**

Here are the Release Blocking issues currently delaying the release:

- [x] None currently

## Progress Tracking

### Release Branching

- [ ] [Test the tutorials](wiki/ReleaseProcess#test-the-tutorials)
- [ ] [Organize a first staff match](wiki/ReleaseProcess#organize-a-first-staff-match)
- [ ] [Prepare for branching](wiki/ReleaseProcess#prepare-for-branching)
- [ ] [Create a `release-XX` branch](wiki/ReleaseProcess#create-a-release-xx-branch)
- [ ] [Adapt Jenkins for the release](wiki/ReleaseProcess#adapt-jenkins-for-the-release)
- [ ] [Announce the start of the process](wiki/ReleaseProcess#announce-the-start-of-the-process)
- [ ] [Prepare next multiplayer lobby in `main`](wiki/ReleaseProcess#prepare-next-multiplayer-lobby-in-main)
- [ ] [Generate next signing key for mods in `main`](wiki/ReleaseProcess#generate-next-signing-key-for-mods-in-main)
- [ ] [Update the Changelog](wiki/ReleaseProcess#update-the-changelog)
- [ ] [Create the next Changelog and Porting guide](wiki/ReleaseProcess#update-the-changelog)
- [ ] [Start writing release announcement](wiki/ReleaseProcess#start-writing-release-announcement)
- [ ] [Start creating the release video](wiki/ReleaseProcess#start-creating-the-release-video)

### String Freeze

- [ ] [Cleanup empty languages](wiki/ReleaseProcess#cleanup-empty-languages)
- [ ] [Announce string freeze](wiki/ReleaseProcess#announce-string-freeze)
- [ ] [Long strings check](wiki/ReleaseProcess#long-strings-check)

### Commit Freeze

- [ ] [Translation check](wiki/ReleaseProcess#translation-check)
- [ ] [Decide on included translations](wiki/ReleaseProcess#decide-on-included-translations)
- [ ] [Organize another staff match](wiki/ReleaseProcess#organize-another-staff-match)

---

Before moving on with Full Freeze, make sure that:

- [ ] At least 10 days have passed since the string freeze announcement
- [ ] Only this ticket remains in the Milestone
- [ ] All previous checkboxes are ticked

---

### Full Freeze

- [ ] [Update appdata.xml](wiki/ReleaseProcess#update-appdataxml)
- [ ] [Freeze Jenkins](wiki/ReleaseProcess#freeze-jenkins)
- [ ] [Confirm full freeze](wiki/ReleaseProcess#confirm-full-freeze)
- [ ] [Announce Release Candidates](wiki/ReleaseProcess#announce-release-candidates)
- [ ] Release Testing: [link to RC]( )

---

When RCs are released, a planned release date is decided with the team and published here. This improves internal coordination and allows us to release announcements on all platforms at the same time.

---

Release Preparation: Please list here all the changes to BuildInstructions and System requirements that will have to be published on release day.

- [ ] Linux changes: TBD
- [ ] Windows changes : TBD
- [ ] macOS changes: TBD
- [ ] BSD changes: TBD

---

### Eve of Release

- [ ] [Schedule the video publication](wiki/ReleaseProcess#schedule-the-video-publication)

### Release

- [ ] [Tag the release commit](wiki/ReleaseProcess#tag-the-release-commit)
- [ ] [Create torrents and checksum files](wiki/ReleaseProcess#create-torrents-and-checksum-files)
- [ ] [Update website](wiki/ReleaseProcess#update-website)
- [ ] [Update BuildInstructions](wiki/ReleaseProcess#update-buildinstructions)
- [ ] [Upload to Sourceforge](wiki/ReleaseProcess#upload-to-sourceforge)
- [ ] [Move the lobby](wiki/ReleaseProcess#move-the-lobby)
- [ ] [Publish announcement](wiki/ReleaseProcess#publish-announcement)
- [ ] [Notify packagers](wiki/ReleaseProcess#notify-packagers)
- [ ] [Post-Release](wiki/ReleaseProcess#post-release)
