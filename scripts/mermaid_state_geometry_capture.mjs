// What a state diagram's SVG says about its geometry, for the capture of
// scripts/mermaid_geometry_capture.mjs: the `describe` the state geometry
// fixture and its fuzz corpus are captured with.

// Runs in the page. Everything is relative to the origin: the centre of the
// first node the document shows.
export const describeStateGeometry = async (root, h, canvas, env) => {
  const { number, absoluteBox, absoluteOrigin, rect, relativePath, text } = h;
  const short = (domId) => (domId.startsWith(`${root.id}-`) ? domId.slice(root.id.length + 1) : domId);
  // A state's DOM id ends in its layout index, which is not part of its id; a
  // note's ends in its counter, which is.
  const stateId = (domId) => {
    const raw = short(domId).replace(/^state-/, "");
    return raw.includes("----note-") ? raw : raw.replace(/-\d+$/, "");
  };
  const nodeElements = [...root.querySelectorAll("g.node")];
  const origin = nodeElements.length ? absoluteOrigin(nodeElements[0]) : { x: 0, y: 0 };
  const about = (box) => (box ? rect(box.x - origin.x, box.y - origin.y, box.width, box.height) : null);
  const localTo = (box, centre) => (box ? rect(box.x - centre.x, box.y - centre.y, box.width, box.height) : null);
  const pointAbout = (p) => ({ x: number(p.x - origin.x), y: number(p.y - origin.y) });

  // The transitions as the database hands them to the layout.
  const diagram = await env.mermaid.mermaidAPI.getDiagramFromText(env.fixture.source);
  const logical = new Map(diagram.db.getData().edges.map((edge) => [edge.id, edge]));

  const nodes = nodeElements.map((element) => {
    const centre = absoluteOrigin(element);
    const shape = element.querySelector(
      ":scope > rect, :scope > circle, :scope > g > rect, :scope > g > path, :scope > g.outer-path > path");
    const labels = [...element.querySelectorAll(":scope > g.label > foreignObject")].map((foreignObject) => ({
      text: text(foreignObject),
      // The box the SVG gives the text; the rows of a titled state keep the
      // height of one row there, so the height the text takes is read too.
      rect: about(absoluteBox(foreignObject)),
      contentHeight: foreignObject.firstElementChild?.getBoundingClientRect().height ?? 0,
    }));
    const divider = element.querySelector(":scope > g > line.divider");
    const dividerCentre = divider ? absoluteOrigin(divider) : null;
    return {
      id: stateId(element.id),
      centre: pointAbout(centre),
      box: localTo(shape ? absoluteBox(shape) : null, centre),
      labels,
      divider: divider
        ? {
            from: pointAbout(new DOMPoint(Number(divider.getAttribute("x1")), Number(divider.getAttribute("y1"))).matrixTransform(h.elementMatrix(divider))),
            to: pointAbout(new DOMPoint(Number(divider.getAttribute("x2")), Number(divider.getAttribute("y2"))).matrixTransform(h.elementMatrix(divider))),
          }
        : null,
    };
  });

  const clusters = [...root.querySelectorAll("g.clusters > g")].map((element) => ({
    id: stateId(element.id),
    rects: [...element.querySelectorAll("rect")].map((box) => ({
      kind: box.getAttribute("class") ?? "",
      rect: about(absoluteBox(box)),
    })),
    title: (() => {
      const foreignObject = element.querySelector("g.cluster-label > foreignObject");
      return foreignObject ? { text: text(foreignObject), rect: about(absoluteBox(foreignObject)) } : null;
    })(),
  }));

  const edges = [...root.querySelectorAll('g.edgePaths path[data-et="edge"]')].map((element) => {
    const id = element.getAttribute("data-id");
    const data = logical.get(id);
    const group = [...root.querySelectorAll("g.edgeLabel")].find(
      (candidate) => candidate.querySelector(":scope > g.label")?.getAttribute("data-id") === id);
    const foreignObject = group?.querySelector("foreignObject");
    const decoded = element.getAttribute("data-points")
      ? JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(element.getAttribute("data-points")), (c) => c.charCodeAt(0))))
      : [];
    const matrix = h.elementMatrix(element);
    // A path cut where it enters or leaves a composite stops short of its
    // waypoints: the path no longer starts or ends where they do.
    const length = element.getTotalLength();
    const first = element.getPointAtLength(0);
    const last = element.getPointAtLength(length);
    const far = (point, waypoint) => Math.hypot(point.x - waypoint?.x, point.y - waypoint?.y) > 1e-3;
    const cut = decoded.length > 0 && (far(first, decoded[0]) || far(last, decoded[decoded.length - 1]));
    return {
      id,
      cut,
      from: data?.start ?? null,
      to: data?.end ?? null,
      markerEnd: /stateDiagram-(barbEnd|barbNeo)/.exec(element.getAttribute("marker-end") ?? "")?.[1] ?? null,
      points: decoded.map((point) => pointAbout(new DOMPoint(point.x, point.y).matrixTransform(matrix))),
      path: relativePath(element.getAttribute("d"), element, origin.x, origin.y),
      label: group?.getAttribute("transform") && foreignObject
        ? { text: text(foreignObject), rect: about(absoluteBox(foreignObject)) }
        : null,
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
