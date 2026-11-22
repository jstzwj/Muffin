(diagram_type) @type
(direction) @constant

[
  "title"
  "accTitle"
  "accDescr"
] @keyword

(title_statement text: (line_text) @string)
(accessibility_title_statement text: (line_text) @string)
(accessibility_description_statement text: (line_text) @string)
(accessibility_description_statement block: (brace_text) @string)

(directive) @attribute
(comment) @comment
(frontmatter_delimiter) @preproc

[
  "direction"
  "subgraph"
  "end"
  "classDef"
  "class"
  "state"
  "note"
  "for"
  "style"
  "linkStyle"
  "interpolate"
  "click"
  "href"
  "call"
] @keyword

(flow_node id: (identifier) @variable)
(standalone_flow_node id: (identifier) @variable)
(class_def_statement name: (identifier) @type)
(flow_diagram
  (flow_statement
    (class_statement
      targets: (identifier_list (identifier) @variable)
      class_name: (identifier) @type)))
(style_statement target: (identifier) @variable)
(link_style_statement target: (number_list (number) @number))
(link_style_statement target: "default" @constant)
(click_statement target: (identifier) @variable)
(subgraph_statement id: (identifier) @namespace)

(class_annotation
  ":::" @punctuation.special
  name: (identifier) @type)

(style_property
  name: (style_property_name) @property
  ":" @punctuation.delimiter
  value: (style_value) @string.special)

(flow_edge operator: (arrow) @operator)
(flow_edge
  operator: (arrow_start) @operator
  operator_end: (arrow) @operator)
(edge_id id: (identifier) @label)
(edge_label text: (_) @string)

(shape_data) @attribute

[
  "["
  "]"
  "("
  ")"
  "{"
  "}"
  "[["
  "]]"
  "(("
  "))"
  "((("
  ")))"
  "(["
  "])"
  "[("
  ")]"
  "{{"
  "}}"
  "[/"
  "/]"
  "[\\"
  "\\]"
  ">"
] @punctuation.bracket

[
  "group"
  "service"
  "junction"
  "in"
  "complex"
  "complicated"
  "clear"
  "chaotic"
  "confusion"
  "places"
  "contains"
  "entity"
  "data"
  "dateFormat"
  "section"
  "after"
  "gwt"
  "tf"
  "timeframe"
  "rf"
  "resetframe"
  "commit"
  "branch"
  "merge"
  "checkout"
  "switch"
  "cherry-pick"
  "showInfo"
  "showData"
  "quadrant"
  "axis"
  "curve"
  "showLegend"
  "ticks"
  "max"
  "min"
  "graticule"
  "size"
  "evolution"
  "anchor"
  "component"
  "label"
  "evolve"
  "pipeline"
  "note"
  "annotations"
  "annotation"
  "accelerator"
  "deaccelerator"
] @keyword

(architecture_group_statement id: (identifier) @namespace)
(architecture_group_statement parent: (identifier) @namespace)
(architecture_service_statement id: (identifier) @variable)
(architecture_service_statement parent: (identifier) @namespace)
(architecture_junction_statement id: (identifier) @variable)
(architecture_junction_statement parent: (identifier) @namespace)
(architecture_edge_statement
  source: (identifier) @variable
  edge: (architecture_arrow) @operator
  target: (identifier) @variable)

(architecture_icon) @attribute
(architecture_group_marker) @attribute
(architecture_title) @string
(architecture_alignment_statement "align" @keyword member: (identifier) @variable)
(architecture_alignment_direction) @constant
(architecture_port) @constant

(ishikawa_label) @string

(sankey_field) @string
(sankey_number) @number
(sankey_link_statement value: (sankey_quoted_field) @number)
(sankey_link_statement "," @punctuation.delimiter)

(xychart_orientation) @constant
(xychart_axis_name) @keyword
(xychart_plot_kind) @keyword
(xychart_text) @string
(xychart_number) @number
(xychart_range "-->" @operator)

(class_note_statement
  target: (identifier) @type)
(class_note_text "\"" @punctuation.delimiter)
(class_note_text_fragment) @string

(class_relationship_statement
  source: (identifier) @type
  operator: (class_relationship_operator) @operator
  target: (identifier) @type)

(class_member_statement class: (identifier) @type)
(class_block name: (identifier) @type)
(class_entity_statement name: (identifier) @type)
(class_property visibility: (class_visibility) @operator)
(class_property type: (identifier) @type)
(class_property name: (identifier) @property)
(class_method visibility: (class_visibility) @operator)
(class_method type: (identifier) @type)
(class_method name: (identifier) @function)

((er_relationship_statement
  source: (identifier) @type
  relationship: (er_relationship_label) @keyword
  target: (identifier) @type)
 (#set! priority 110))

((er_relationship_statement
  source: (identifier) @type
  operator: (er_relationship_operator) @operator
  target: (identifier) @type)
 (#set! priority 110))
(er_relationship_statement label: (_) @string)

(er_entity_block
  name: (identifier) @type)
(er_entity_alias text: (_) @string)
(er_entity_attribute_statement
  type: (identifier) @type
  name: (identifier) @property)
(er_attribute_key) @constant
(er_entity_attribute_statement
  comment: (quoted_string) @string)

((er_attribute_statement
  entity: (identifier) @type
  attribute: (identifier) @property)
 (#set! priority 110))

(gantt_date_format_statement format: (gantt_date_format) @string.special)
(gantt_section_statement name: (gantt_section_name) @namespace)
(gantt_task_statement label: (gantt_task_label) @string)
(gantt_task_statement status: (gantt_task_status) @keyword)
(gantt_task_statement id: (identifier) @label)
(gantt_task_statement date: (gantt_date) @number)
(gantt_task_statement duration: (gantt_duration) @number)
(gantt_task_statement dependency: (identifier) @label)

(git_option name: (git_option_name) @property)
(git_option ":" @punctuation.delimiter)
((git_option
  name: (git_option_name) @_git_option_name
  value: (quoted_string) @string)
 (#eq? @_git_option_name "msg"))
((git_option
  name: (git_option_name) @_git_option_name
  value: (number) @number)
 (#eq? @_git_option_name "order"))
((git_option
  name: (git_option_name) @_git_option_name
  value: [
    (identifier)
    (quoted_string)
  ] @label)
 (#match? @_git_option_name "^(id|tag|parent)$"))
((git_option
  name: (git_option_name) @_git_option_name
  value: (identifier) @constant)
 (#eq? @_git_option_name "type"))
((git_graph_statement name: (identifier) @label)
 (#set! priority 110))
((git_graph_statement name: (quoted_string) @label)
 (#set! priority 110))
((git_graph_statement branch: (identifier) @label)
 (#set! priority 110))
((git_graph_statement branch: (quoted_string) @label)
 (#set! priority 110))

(journey_section_statement name: (journey_section_name) @namespace)
(journey_task_statement label: (journey_task_label) @string)
(journey_task_statement score: (number) @number)
(journey_task_statement actor: (identifier) @variable)

(mindmap_root_keyword) @keyword
(mindmap_icon_marker) @keyword
(mindmap_icon_name) @attribute
(mindmap_label_fragment) @string

(packet_statement label: (quoted_string) @string)

(pie_statement
  label: (quoted_string) @string
  value: (number) @number)

(quadrant_axis_statement
  axis: (quadrant_axis_name) @keyword
  from: (quadrant_axis_text) @string
  (quadrant_axis_arrow) @operator
  to: (quadrant_axis_text) @string)

(quadrant_section_statement
  number: (number) @constant
  label: (quadrant_text) @string)

(quadrant_point_statement
  label: (quadrant_point_label) @variable
  x: (number) @number
  y: (number) @number)

(radar_axis name: (identifier) @variable)
(radar_curve name: (identifier) @variable)
(radar_option name: (radar_option_name) @property)
(radar_option value: (number) @number)
(radar_option value: (boolean) @boolean)
(radar_option value: ["circle" "polygon"] @constant)

((requirement_block
  kind: (requirement_kind) @keyword
  name: (identifier) @label)
 (#set! priority 110))

((requirement_property_statement
  name: (requirement_property_name) @property
  ":" @punctuation.delimiter
  value: [
    (requirement_property_value)
    (quoted_string)
  ] @string)
 (#set! priority 110))

((requirement_property_statement
  name: (requirement_property_name) @property
  value: (requirement_id) @label)
 (#set! priority 111))

((requirement_relationship_statement
  source: (identifier) @label
  operator: (requirement_relationship_operator) @operator
  relationship: (requirement_relationship_type) @keyword
  target: (identifier) @label)
 (#set! priority 110))

(sequence_message_statement
  source: (sequence_actor) @variable
  arrow: (sequence_arrow) @operator
  target: (sequence_actor) @variable)

(sequence_participant_keyword) @keyword
(sequence_participant_statement name: (sequence_actor) @variable)
(sequence_as_keyword) @keyword
(sequence_autonumber_keyword) @keyword
(sequence_autonumber_off) @keyword
(sequence_activation) @operator
(sequence_connection) @operator
(sequence_loop_keyword) @keyword
(sequence_note_keyword) @keyword
(sequence_of_keyword) @keyword
(sequence_message_text_fragment) @string
(sequence_note_statement
  target: (sequence_actor_list (sequence_actor) @variable))
(sequence_note_side_position) @keyword
(sequence_note_over_position) @keyword

(state_declaration_statement name: (identifier) @type)
(state_marker) @constant
(state_transition_statement source: (identifier) @type)
(state_transition_statement target: (identifier) @type)
(state_transition_statement label: (state_transition_label) @string)
(state_transition_statement
  arrow: (state_arrow) @operator)

(state_diagram
  (class_statement
    targets: (identifier_list (identifier) @type)
    class_name: (identifier) @type))

(timeline_section_statement name: (timeline_section_name) @namespace)
(timeline_event_statement time: (timeline_time) @variable)
(timeline_event_statement ":" @punctuation.delimiter)
(timeline_text_fragment) @string

(tree_item_statement name: (quoted_string) @string)
(tree_icon_annotation) @attribute
(tree_description_annotation) @comment

(quoted_string) @string
(number) @number
(boolean) @boolean
(label_text_fragment) @string
(html_tag) @tag

[
  ":"
  ","
  ";"
  "@"
  "|"
] @punctuation.delimiter
