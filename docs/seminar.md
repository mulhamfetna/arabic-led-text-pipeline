# Arabic text on LED dot-matrix displays

**Mulham Fetna** · [ORCID 0009-0006-4432-798X](https://orcid.org/0009-0006-4432-798X)

---

## 1. A problem you have already seen

Walk through any market in the region and you pass dozens of scrolling LED signs. Look closely at
the Arabic ones. A great many are wrong.

Letters that should join stand apart. Words break into disconnected stumps. Some signs give up and
write Arabic in Latin letters. Others display a single fixed image, produced once on a computer,
which can never be changed without going back to that computer.

This is not a rare defect. It is the normal condition of the technology.

## 2. Why Arabic is harder than Latin here

Arabic is **cursive** and **right-to-left**, and both properties break an assumption built into
these devices.

**A letter's shape depends on its neighbours.** Arabic letters take up to four forms — isolated,
initial, medial, final. The letter ع appears as `ع` standing alone, `عـ` at the start of a word,
`ـعـ` in the middle, `ـع` at the end. One character, four different shapes, chosen by context.

**Certain pairs must fuse.** ل followed by ا is not written as two letters; it becomes the single
form `لا`. This is mandatory in correct Arabic, not a stylistic flourish.

**Order and direction interact.** Arabic runs right-to-left, and a line mixing Arabic with digits
or Latin words needs a defined rule for what appears where.

Latin script has none of these properties. A system designed around Latin — as most display
hardware is — has no mechanism for any of them.

## 3. Where the mismatch actually lies

Conventional sign controllers store text as **one fixed image per character code**. Character code
in, stored picture out, always the same picture.

That model cannot express Arabic, because choosing the correct shape requires knowing what comes
*before and after* — and a per-character lookup has no way to consult anything.

> The limitation is **architectural, not cosmetic.** It is not solved by better fonts or higher
> resolution, because the design has no place to ask the question Arabic requires.

This is why the problem persists in commercial products rather than having quietly been fixed.

## 4. Prior work — an honest assessment

It would be convenient to claim nobody has addressed this. That claim is false, and a structured
survey of the field disproved it.

- **Arabic shaping on microcontrollers exists.** At least one widely used open graphics library
  implements contextual letter shaping and bidirectional text, and there are smaller standalone
  efforts as well. Credit is due, and recorded.
- **The libraries that actually drive LED dot-matrix panels do not.** They supply Arabic *glyphs*
  but no contextual shaping — in one prominent case the maintainer declined the feature
  deliberately, and user reports of disconnected letters have remained open for roughly six years.
- **One widely used plugin ships an "Arabic font"** whose own documentation states it should not be
  used to render ordinary Arabic text.
- **Commercially, Arabic is not a property of the sign's firmware.** Where it works at all, it is
  handled inside closed desktop software before anything reaches the display.

The survey is published in `related-work.md`, separating claims verified against primary sources
from those that are not — including one case where automated verification returned a false negative
and had to be corrected by hand.

**The remaining gap is therefore narrow and specific:** an open, Unicode-correct Arabic text
pipeline for **low-resolution, single-colour LED dot-matrix panels**. Not "nobody solved Arabic".

## 5. What this project is

An open system that displays correctly shaped Arabic on inexpensive LED matrix hardware, driven
from an ordinary phone, with no proprietary software anywhere in the chain.

Design goals, stated at the level of what the system guarantees:

| Goal | Why it matters |
|---|---|
| **Correct Arabic** — proper joining and required ligatures | The point of the exercise |
| **No app to install** | Signs are operated by shopkeepers, not engineers |
| **No internet, no router** | Must work standing alone in a shop or mosque |
| **What you preview is what appears** | Operators need to trust the result before sending |
| **Documented open protocol** | Any microcontroller can implement the receiving side |
| **Inexpensive, available hardware** | Adoption depends on cost |

## 6. Status

Working hardware, not a proposal.

| Capability | Status |
|---|---|
| Driving LED matrix hardware, arbitrary panel geometry | ✅ working |
| Self-hosted wireless operation, no router or internet | ✅ working |
| Phone-based control interface | ✅ working |
| Correct Arabic shaping end to end | ✅ working |
| Static and scrolling display, with direction control | ✅ working |
| Faithful on-screen preview | ✅ working |
| Documented, checksummed transfer protocol | ✅ working |
| Larger panel types | ⬜ designed, not yet tested |

Every capability listed as working was confirmed on physical hardware.

## 7. Open questions

The parts that remain genuinely unresolved, stated without inflation:

**Arabic legibility at very small pixel heights.** The survey found **no published work** on Arabic
glyph design at the sizes these panels impose. Latin script has decades of accumulated bitmap-font
craft; Arabic at comparable sizes is largely uncharted. Whether readable Arabic at this scale
requires purpose-designed letterforms — rather than reduction of existing typefaces — is an open
research question, and one this project is positioned to investigate.

**Consistency across different devices.** Ensuring identical output regardless of which device
drives the sign raises questions that are understood but not fully closed.

**Whether the overall approach is novel.** No prior art was found for the specific architecture
used here — but no exhaustive search has been performed either, so **novelty is not claimed.**
Establishing this properly is recorded as outstanding work.

**Quantifying the central engineering trade-off.** A widely respected maintainer judged one
plausible approach infeasible on resource grounds. That judgement has never been measured
empirically. Turning received wisdom into evidence is outstanding work.

## 8. Openness and citation

Every closed alternative locks Arabic support inside desktop software that cannot be inspected,
extended, or run on the device itself.

This project is released under **AGPL-3.0-or-later**, so derivatives remain open — including when
operated as a network service. It is archived with a DOI so it can be cited rather than merely
downloaded.

## 9. Summary

Arabic on LED signs is usually wrong because those devices store one fixed picture per character,
while Arabic letters change shape according to their neighbours. The limitation is architectural, so
it is not fixed by better fonts.

This project builds an open system that renders correctly shaped Arabic on inexpensive LED matrix
hardware, controlled from an ordinary phone with no app, no router and no internet. It is working
hardware with a documented open protocol, targeting a gap that a survey of prior work confirmed is
real: **an open, Unicode-correct Arabic pipeline for low-resolution single-colour LED matrices.**

The most interesting question it raises is not an engineering one. It is typographic: **what does
Arabic need to look like to stay readable when there are almost no pixels left to draw it with?**
Nobody appears to have published an answer.
