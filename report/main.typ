// IMPORTS
#import "content/intro.typ" as intro
#import "content/lit_review.typ" as lit
#import "content/method.typ" as meth
#import "content/bom.typ" as bom
#import "content/time.typ" as time

// FORMAT SETUP
#set text(font: "cambria")
#set par(first-line-indent: 2em)
#set par(justify: true)

// VARIABLES
#let title = smallcaps[Highland Plastics: Thermal Performance\ Evaluation Report]
#let robert = "Alm, R."
#let sam = "Hass, S."
#let connor = "Bendele, C."
#let ethan = "Grentz, E."
#let matthew = "Coeling, M."
#let eli = "Dawson, E."
#let class = smallcaps[EGR 489WI Senior Design Project]

// PARAGRAPHS
#let decree = [
A PROJECT REPORT\ PREPARED IN PARTIAL FULFILLMENT OF THE\ REQUIREMENT FOR THE DEGREE OF\ BACHELOR OF SCIENCE\ IN\ ENGINEERING TECHNOLOGY
]
#let ethics = [
A team consisting of the individuals listed below solely prepared the work submitted in this report and it is original. Excerpts from others’ work have been clearly identified, their work acknowledged within the text and listed in the list of references. All of the engineering drawings, computer programs, formulations, design work, prototype development and testing reported in this document are also original and prepared by the same team of students. 
  ]

// TITLE PAGE
#align(center + horizon)[
  #image("watermark.png", width: 5cm)

  #text(16pt, class)
  
  #decree
  
  #text(20pt, strong(title))
  
  #robert #sam #connor #ethan #matthew #eli

  Faculty Advisor: Dr. Mohamed Awad
]
#pagebreak()

#show heading: smallcaps
= Ethics Statement and Signatures
#ethics
#pagebreak()

#set heading(numbering: "1.1.")
#outline()
#pagebreak()

//1 REPORT BEGINS
= Introduction
== Project Overview
#intro.project_overview
== Customers
#intro.customers
== Constraints
#intro.constraints
== Stakeholders
#intro.stakeholders
== Project Goals
#intro.project_goals
== Customer Needs
#intro.customer_needs
== Design Targets
#intro.design_targets

//2 LITERATURE
= Literature Review
== UL Standards
#lit.standards
== Fuel and Air Standards
#lit.fuel_air
== Safety Critical Programming Standards
#lit.programming
== Analysis
#lit.analysis
//3 METHODS
= Methodology
== Design Concepts
=== Basic Design: "Modular Table and Data Collection"
#meth.design1
=== Design 2: "Basic + PID Loop and Predictive Outputs"
#meth.design2
=== Design 3: "Basic + PID Loop, Preheat Barrier, and Thermocouples"
#meth.design3
== Concept Selection
#meth.concept
== Proposed Prototype Design
#meth.prototype
#pagebreak()
== Timeline
#time
== Breakdown and Distribution of Work
#meth.work
#set par(justify: false)

== Bill of Materials
#bom
