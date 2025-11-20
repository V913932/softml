# SoftML

**SoftML** is a lightweight, forgiving parser for SoftML documents, implemented in C. It parses SoftML into an internal **DOM (Document Object Model)** structure that can be easily navigated, queried, and manipulated. SoftML allows for flexible and loosely structured markup, making it resilient to missing or malformed tags.

---

## Features

- **Forgiving Parsing:** SoftML parser continues parsing even with unclosed or malformed tags.
- **Lightweight:** Minimal dependencies; implemented in pure C.
- **DOM Representation:** Provides a structured tree representation of the document.
- **Query API:** Easily find child elements, attributes, or text content.
- **Flexible:** Works with both quoted and unquoted attributes.

---

## Installation

Clone the repository:

```bash
git clone https://github.com/V913932/softml.git
cd softml
