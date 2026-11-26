// What a class diagram's SVG says about its geometry, for the capture of
// scripts/mermaid_geometry_capture.mjs: the `describe` the class geometry
// fixture and its fuzz corpus are captured with.

// Runs in the page. Everything is relative to the origin: the position of the
// first declared class, which is where Muffin's own origin is.
export const describeClassGeometry = async (root, h, canvas, env) => {
  const { number, absoluteBox, absoluteOrigin, translate, rect, relativePath, labelBox, text } = h;
  const short = (domId) => (domId.startsWith(`${root.id}-`) ? domId.slice(root.id.length + 1) : domId);
  const numbersOf = (data) => data.match(/-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/gi) ?? [];
  const markerType = (value) => {
    const m = /(?:aggregation|composition|extension|dependency|lollipop)(?:Start|End)/.exec(value ?? "");
    return m ? m[0].replace(/(?:Start|End)$/, "") : null;
  };
  // A label's box in the coordinates of the group that carries it: its own
  // box at its translate, the group's translate added.
  const placedBox = (label, [gx, gy] = [0, 0]) => {
    if (!label) return null;
    const box = labelBox(label);
    if (!box) return null;
    const [lx, ly] = translate(label);
    return { ...rect(gx + lx + box.x, gy + ly + box.y, box.width, box.height), text: text(label) };
  };

  const nodeElements = [...root.querySelectorAll("g.nodes > g.node")];
  const isClass = (node) => node.id.includes("-classId-");
  const classId = (node) => short(node.id).replace(/^classId-/, "").replace(/-\d+$/, "");

  // The relationships as the database hands them to the layout, in its order:
  // a relationship from a class to itself is three paths. The renderer keeps
  // its label and cardinality groups by path id, which tells whose they are
  // without reading the document's order into it. It renders off the document,
  // though, and the capture reads the copy that is attached: a group is found
  // in the copy by its place among those of its kind.
  const diagram = await env.mermaid.mermaidAPI.getDiagramFromText(env.fixture.source);
  const data = diagram.db.getData();
  const { edgeLabels, terminalLabels } = await import(env.auxiliaryModule);
  const attached = (selection) => {
    const element = selection?.node();
    if (!element) return null;
    const kind = element.getAttribute("class").split(" ")[0];
    const originals = [...element.closest("svg").querySelectorAll(`g.${kind}`)];
    return [...root.querySelectorAll(`g.${kind}`)][originals.indexOf(element)] ?? null;
  };

  const firstClass = data.nodes.find((node) => node.shape === "classBox");
  const originNode = nodeElements.find((node) => isClass(node) && classId(node) === firstClass?.id) ?? nodeElements[0];
  const origin = originNode ? absoluteOrigin(originNode) : { x: 0, y: 0 };
  const about = (box) =>
    box ? rect(box.x - origin.x, box.y - origin.y, box.width, box.height) : null;
  const pointAbout = (p) => ({ x: number(p.x - origin.x), y: number(p.y - origin.y) });

  const nodes = nodeElements.map((node) => {
    const domId = short(node.id);
    // The outline is a path whose data is in the node's own coordinates: its
    // corners are the extreme numbers of it.
    // A namespace nothing sits in is drawn as a box: its outline is the rect
    // that carries the class itself, and its label is the group on top.
    const outline =
      node.querySelector(".outer-path path, .label-container path, .label-container rect") ??
      node.querySelector(":scope > rect.label-container");
    let local = null;
    if (outline) {
      if (outline.tagName === "path") {
        const tokens = numbersOf(outline.getAttribute("d") ?? "");
        const xs = [], ys = [];
        for (let index = 0; index + 1 < tokens.length; index += 2) {
          xs.push(Number(tokens[index]));
          ys.push(Number(tokens[index + 1]));
        }
        local = { x: Math.min(...xs), y: Math.min(...ys), width: Math.max(...xs) - Math.min(...xs), height: Math.max(...ys) - Math.min(...ys) };
      } else {
        local = {
          x: Number(outline.getAttribute("x")),
          y: Number(outline.getAttribute("y")),
          width: Number(outline.getAttribute("width")),
          height: Number(outline.getAttribute("height")),
        };
      }
    }
    const rows = (name) => {
      const container = node.querySelector(`.${name}-group`);
      if (!container) return [];
      return [...container.querySelectorAll(":scope > g.label")]
        .map((label) => placedBox(label, translate(container)))
        .filter(Boolean);
    };
    return {
      id: isClass(node) ? classId(node) : domId,
      note: !isClass(node),
      centre: pointAbout(absoluteOrigin(node)),
      // The outline in the node's own coordinates, whose centre is its origin.
      local,
      // A divider is a line a hundredth of a pixel off the straight, from the
      // node's left edge to its right: where it starts and where it ends.
      dividers: [...node.querySelectorAll("g.divider")].map((divider) => {
        const tokens = numbersOf(divider.querySelector("path")?.getAttribute("d") ?? "");
        const at = (index) => ({ x: Number(tokens[index]), y: Number(tokens[index + 1]) });
        return { from: at(0), to: at(tokens.length - 2) };
      }),
      annotation: rows("annotation"),
      name: rows("label"),
      members: rows("members"),
      methods: rows("methods"),
      noteLabel: placedBox(node.querySelector("g.label.noteLabel") ?? node.querySelector(":scope > g.label")),
    };
  });

  const clusters = [...root.querySelectorAll("g.clusters > g.cluster")].map((cluster) => {
    const box = cluster.querySelector("rect");
    // The title's group sits in the document, not in the cluster.
    const titleGroup = cluster.querySelector("g.cluster-label");
    const titleBox = titleGroup ? labelBox(titleGroup) : null;
    let title = null;
    if (titleBox) {
      const at = absoluteOrigin(titleGroup);
      title = {
        text: text(titleGroup),
        rect: about({ x: at.x + titleBox.x, y: at.y + titleBox.y, width: titleBox.width, height: titleBox.height }),
      };
    }
    return { id: short(cluster.id), bounds: box ? about(absoluteBox(box)) : null, title };
  });

  const pathElements = [...root.querySelectorAll("g.edgePaths > path")];
  let pathIndex = 0;
  const edges = data.edges.map((edge) => {
    // The paths come in the database's order; a relationship from a class to
    // itself is three, named after the class and not after the relationship.
    // Of several a class has, only the last is drawn: each lays its three out
    // under the same names, so each replaces the one before it.
    const own = data.edges.filter((candidate) => candidate.start === edge.start && candidate.end === edge.end);
    const superseded = edge.start === edge.end && own.indexOf(edge) !== own.length - 1;
    const count = superseded ? 0 : edge.start === edge.end ? 3 : 1;
    const paths = pathElements.slice(pathIndex, pathIndex + count);
    pathIndex += count;
    let label = null;
    const describedPaths = paths.map((element) => {
      const pathId = short(element.id);
      const group = attached(edgeLabels.get(pathId));
      const box = group?.querySelector("g.label") ? labelBox(group.querySelector("g.label")) : null;
      // positionEdgeLabel() only moves a label a relationship has.
      if (group?.getAttribute("transform") && box && Number(box.width) > 0) {
        const [lx, ly] = translate(group.querySelector("g.label"));
        const at = absoluteOrigin(group);
        label = {
          text: text(group),
          rect: about({ x: at.x + lx + box.x, y: at.y + ly + box.y, width: box.width, height: box.height }),
        };
      }
      return {
        // The waypoints the layout gave the path, which it carries.
        points: element.getAttribute("data-points")
          ? (() => {
              const decoded = JSON.parse(
                new TextDecoder().decode(Uint8Array.from(atob(element.getAttribute("data-points")), (c) => c.charCodeAt(0))),
              );
              const matrix = h.elementMatrix(element);
              return decoded.map((point) =>
                pointAbout(new DOMPoint(point.x, point.y).matrixTransform(matrix)),
              );
            })()
          : [],
        path: relativePath(element.getAttribute("d"), element, origin.x, origin.y),
      };
    });
    // A cardinality: its text box from the group's origin, and the box
    // setTerminalWidth() clips it to: 9px per character by 12px, from the text
    // box's top-left corner (a stylesheet length wins over the size attributes
    // the text was measured at). A start label is centred by an inner group;
    // an end one is laid out on the group itself, its corner at the origin.
    const terminal = (group) => {
      const foreignObject = group.querySelector("foreignObject");
      const host = foreignObject?.parentElement?.classList.contains("inner") ? foreignObject.parentElement : group;
      const [hx, hy] = host === group ? [0, 0] : translate(host);
      const centre = absoluteOrigin(group);
      const label = labelBox(group) ?? { x: 0, y: 0, width: 0, height: 0 };
      const stylesheet = foreignObject ? getComputedStyle(foreignObject) : null;
      const width = stylesheet ? Number.parseFloat(stylesheet.width) : label.width;
      const height = stylesheet ? Number.parseFloat(stylesheet.height) : label.height;
      const x = centre.x - origin.x + hx + label.x;
      const y = centre.y - origin.y + hy + label.y;
      return {
        text: text(group),
        rect: rect(x, y, label.width, label.height),
        box: rect(x, y, width, height),
      };
    };
    const pathIds =
      count === 3
        ? [`${edge.start}-cyclic-special-1`, `${edge.start}-cyclic-special-mid`, `${edge.start}-cyclic-special-2`]
        : [edge.id];
    // The tables' entries are d3 selections of the groups.
    const holders = pathIds.map((id) => terminalLabels.get(id) ?? {});
    const terminals = {};
    const holderKeys = {
      startLabelLeft: "startLeft",
      startLabelRight: "startRight",
      endLabelLeft: "endLeft",
      endLabelRight: "endRight",
    };
    for (const [name, key] of Object.entries(holderKeys)) {
      const group = attached(holders.find((holder) => holder[key])?.[key]);
      if (group) terminals[name] = terminal(group);
    }
    const first = paths[0];
    const last = paths[paths.length - 1];
    return {
      id: edge.id,
      from: edge.start,
      to: edge.end,
      superseded,
      pattern: /edge-pattern-(dashed|dotted)/.exec(first?.getAttribute("class") ?? "")?.[1] ?? "solid",
      markerStart: markerType(first?.getAttribute("marker-start")),
      markerEnd: markerType(last?.getAttribute("marker-end")),
      paths: describedPaths,
      label,
      terminals,
    };
  });

  const bbox = root.getBBox();
  return {
    origin: { x: number(origin.x), y: number(origin.y) },
    canvas: { ...canvas, content: rect(bbox.x - origin.x, bbox.y - origin.y, bbox.width, bbox.height) },
    nodes,
    clusters,
    edges,
  };
};
