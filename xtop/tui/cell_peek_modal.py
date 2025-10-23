#!/usr/bin/env python3
"""
Modal dialog for displaying detailed cell information.
Used for peeking into complex data like histogram visualizations.
"""

import logging
from textual.app import ComposeResult
from textual.containers import Container, Vertical, Horizontal, ScrollableContainer
from textual.widgets import Label, Static, DataTable
from textual.screen import ModalScreen
from textual.binding import Binding
from typing import Dict, List, Tuple, Any, Optional
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from core.time_utils import TimeUtils
from core.histogram_formatter import HistogramFormatter
from core.heatmap_visualizer import HeatmapVisualizer
from core.peek_providers import HistogramPeekProvider, parse_histogram_string, parse_stack_trace


class CellPeekModal(ModalScreen[None]):
    """Modal screen for displaying detailed cell information"""
    
    BINDINGS = [
        Binding("escape", "dismiss", "Close", priority=True),
        Binding("q", "dismiss", "Close"),
    ]
    
    CSS = """
    CellPeekModal {
        align: center middle;
    }
    
    #peek-container {
        width: 80%;
        height: 80%;
        background: $surface;
        border: thick $primary;
        padding: 1;
    }
    
    #peek-header {
        height: 3;
        background: $primary;
        padding: 0 1;
        margin-bottom: 1;
    }
    
    #peek-content {
        height: 1fr;
    }
    
    .peek-footer {
        height: 3;
        background: $surface-lighten-1;
        padding: 0 1;
        margin-top: 1;
    }
    
    #peek-title {
        text-style: bold;
        color: $text;
    }
    
    DataTable {
        height: 100%;
    }
    
    .dim {
        opacity: 0.6;
    }
    """
    
    def __init__(self, 
                 column_name: str,
                 cell_value: Any,
                 full_data: Optional[Dict] = None,
                 **kwargs):
        """Initialize peek modal
        
        Args:
            column_name: Name of the column being peeked
            cell_value: Value of the cell being peeked
            full_data: Full row data for context
        """
        super().__init__(**kwargs)
        # Standardize column name to lowercase
        self.column_name = column_name.lower() if column_name else column_name
        self.cell_value = cell_value
        self.full_data = full_data or {}
    
    def compose(self) -> ComposeResult:
        """Build the modal UI"""
        with Container(id="peek-container"):
            with Vertical(id="peek-header"):
                yield Label(f"Details: {self.column_name}", id="peek-title")
            
            with ScrollableContainer(id="peek-content"):
                # Display based on column type
                if self._is_histogram_column():
                    yield from self._compose_histogram_view()
                elif self._is_stack_column():
                    yield from self._compose_stack_view()
                else:
                    yield from self._compose_default_view()
            
            with Horizontal(classes="peek-footer"):
                yield Label("Press [ESC] or [Q] to close", classes="dim")
    
    def _is_histogram_column(self) -> bool:
        """Check if this is a histogram column"""
        return 'histogram' in self.column_name.lower() or \
               self.column_name.lower().endswith('_hist')
    
    def _is_stack_column(self) -> bool:
        """Check if this is a stack trace column"""
        return 'stack' in self.column_name.lower()
    
    def _compose_default_view(self) -> ComposeResult:
        """Default view for regular data"""
        # Format the value nicely
        formatted_value = self._format_value(self.cell_value)
        yield Static(formatted_value)
        
        # Show context from full row if available
        if self.full_data:
            yield Static("\n[bold]Row Context:[/bold]")
            for key, value in self.full_data.items():
                if key != self.column_name:  # Don't repeat the main value
                    yield Static(f"  {key}: {self._format_value(value)}")
    
    def _compose_histogram_view(self) -> ComposeResult:
        """View for histogram data with heatmap and distribution"""
        # Parse histogram data and create visualization
        histogram_data = parse_histogram_string(self.cell_value)
        
        if not histogram_data:
            yield Static("No histogram data available")
            return
        
        # Import heatmap generator
        try:
            import sys
            import os
            sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
            from core.heatmap import LatencyHeatmap, HeatmapConfig
            
            # Generate heatmap visualization
            config = HeatmapConfig(width=50, height=10, use_color=False)
            heatmap = LatencyHeatmap(config)
            
            # Convert histogram data to format for heatmap
            heatmap_str, _ = heatmap.generate_histogram_heatmap(self.cell_value)
            yield Static(heatmap_str)
            yield Static("\n")
        except Exception as e:
            # Fallback if heatmap fails
            logging.warning(f"Could not generate heatmap: {e}")
        
        # Create latency distribution visualization
        yield Static("[bold]Latency Distribution Details:[/bold]\n")
        
        max_count = max(item[1] for item in histogram_data) if histogram_data else 1
        
        for bucket, count, est_time, _ in histogram_data:
            # Format bucket range
            bucket_str = self._format_latency_range(bucket)
            
            # Create bar
            bar_width = int((count / max_count) * 40) if max_count > 0 else 0
            bar = "█" * bar_width
            
            # Format line
            line = f"{bucket_str:15} {bar:40} {count:8,d} ({est_time:.2f}s)"
            yield Static(line)
    
    def _compose_stack_view(self) -> ComposeResult:
        """View for stack trace data"""
        if not self.cell_value or self.cell_value == '-':
            yield Static("No stack trace available")
            return
        
        yield Static("[bold]Stack Trace:[/bold]\n")
        
        # Parse and format stack trace
        stack_frames = parse_stack_trace(self.cell_value)
        for i, frame in enumerate(stack_frames):
            yield Static(f"  {i:2d}: {frame}")
    
    def _format_value(self, value: Any) -> str:
        """Format a value for display"""
        if value is None or value == '':
            return '-'
        elif isinstance(value, (int, float)):
            return f"{value:,.2f}" if isinstance(value, float) else f"{value:,}"
        else:
            return str(value)
    
    def _format_latency_range(self, bucket_us: int) -> str:
        """Format bucket value into latency range string"""
        next_bucket = bucket_us * 2
        
        def format_latency(us: int) -> str:
            if us >= 1000000:
                return f"{us/1000000:.0f}s"
            elif us >= 1000:
                return f"{us/1000:.0f}ms"
            else:
                return f"{us}μs"
        
        return f"{format_latency(bucket_us)}-{format_latency(next_bucket)}"


class HistogramPeekModal(ModalScreen[None]):
    """Modal for histogram drill-down view with DuckDB query"""
    
    BINDINGS = [
        Binding("escape", "dismiss", "Close", priority=True),
        Binding("q", "dismiss", "Close"),
        Binding("space", "toggle_granularity", "Toggle Time Granularity"),
    ]
    
    CSS = """
    HistogramPeekModal {
        align: center middle;
    }
    
    #histogram-container {
        width: 95%;
        height: 90%;
        background: $surface;
        border: thick $primary;
        padding: 1;
    }
    
    #histogram-header {
        height: 3;
        background: $primary;
        padding: 0 1;
        margin-bottom: 1;
    }
    
    #histogram-title {
        text-style: bold;
        color: $text;
    }
    
    ScrollableContainer {
        height: 1fr;
        overflow-y: auto;
        overflow-x: auto;
    }
    
    Vertical {
        height: auto;
        width: 100%;
    }
    
    RichLog {
        height: auto;
        margin-bottom: 2;
        background: $surface;
        padding: 1;
        width: auto;
    }
    
    DataTable {
        height: auto;
        margin-top: 2;
        margin-bottom: 2;
        border: none;
        background: $surface;
        padding: 0;
    }
    
    DataTable > .datatable--header {
        text-style: bold;
        background: $surface-lighten-1;
    }
    
    #heatmap-section {
        height: auto;
        margin-bottom: 2;
        background: $surface;
        padding: 1;
    }
    
    #table-section {
        height: auto;
        margin-top: 1;
        background: $surface;
        padding: 1;
    }
    
    .peek-footer {
        height: 2;
        padding: 0 1;
        margin-top: 1;
        dock: bottom;
    }
    
    .dim {
        opacity: 0.6;
    }
    """
    
    def __init__(self,
                 column_name: str,
                 histogram_data: str,
                 where_clause: str,
                 datadir: str,
                 engine: Any,
                 query_type: str = 'dynamic',
                 low_time: Optional[Any] = None,
                 high_time: Optional[Any] = None,
                 **kwargs):
        """Initialize histogram peek modal"""
        super().__init__(**kwargs)
        # Standardize column name to lowercase
        self.column_name = column_name.lower() if column_name else column_name
        self.histogram_data = histogram_data
        self.where_clause = where_clause
        self.datadir = datadir
        self.engine = engine
        self.query_type = query_type
        self.low_time = low_time
        self.high_time = high_time
        self._provider = HistogramPeekProvider(engine, datadir)
        
        # Heatmap granularity settings - use TimeUtils constants
        self.granularity_options = [
            TimeUtils.GRANULARITY_HOUR,
            TimeUtils.GRANULARITY_MINUTE,
            TimeUtils.GRANULARITY_SECOND
        ]
        self.current_granularity_index = 2  # Start with HH:MI:S10 (10-second buckets)
        self.heatmap_container_id = None  # Will store reference to heatmap container
        
        # Initialize formatter and visualizer
        self.formatter = HistogramFormatter()
        self.heatmap_viz = HeatmapVisualizer()
    
    def compose(self) -> ComposeResult:
        """Build the histogram UI"""
        with Container(id="histogram-container"):
            with Vertical(id="histogram-header"):
                # Build filter display from where clause
                filter_display = self._extract_filter_display(self.where_clause)
                title = f"Latency Histogram: {self.column_name}"
                if filter_display:
                    title += f" | Filters: {filter_display}"
                yield Label(title, id="histogram-title")
            
            # Use scrollable container for ALL content
            with ScrollableContainer():
                # Vertical container to hold both heatmap and table
                with Vertical():
                    # Container for heatmap section that can be refreshed
                    with Container(id="heatmap-section"):
                        yield from self._generate_heatmap_content()
                    
                    # Data table section with consistent formatting
                    with Container(id="table-section"):
                        yield Static("[bold]Latency Distribution Table:[/bold]")
                        
                        # Prepare the data table
                        table = DataTable(cursor_type="none", zebra_stripes=True, show_cursor=False)
                        table.add_columns(
                            "Latency Range",  # This will be right-aligned in the data
                            "Count",          # Right-aligned header and data
                            "Est. Events/s",  # Right-aligned header and data  
                            "Est. Time (s)",  # Right-aligned header and data
                            "Time %",         # Right-aligned header and data
                            "Visual"          # Left-aligned (visual bar)
                        )
                        
                        self._run_histogram_query_and_populate(table)
                        
                        # Store reference to table (no longer for focus)
                        self.data_table = table
                        
                        # Display the DataTable at its full height
                        yield table
            
            with Horizontal(classes="peek-footer"):
                yield Label("Press [ESC]/[Q] to close, [SPACE] to toggle time granularity", classes="dim")
    
    def on_mount(self) -> None:
        """Modal mounted - no focus needed since table is not interactive"""
        pass
    
    def _generate_heatmap_content(self) -> ComposeResult:
        """Generate heatmap section content"""
        # Show current granularity
        granularity = self.granularity_options[self.current_granularity_index]
        yield Static(f"[bold]Time-Series Heatmap (Granularity: {granularity}):[/bold]")
        
        # Show selected time range using TimeUtils
        time_range_str = TimeUtils.format_time_range(self.low_time, self.high_time)
        yield Static(f"[dim]Selected time range: {time_range_str}[/dim]")
        
        try:
            # Import heatmap modules
            import sys
            import os
            sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
            from core.heatmap import LatencyHeatmap, HeatmapConfig
            
            # Run time-series query for heatmap
            heatmap_data = self._run_timeseries_query()
            
            if heatmap_data is None:
                # Error occurred during query execution
                yield Static("[red bold]Error:[/red bold] Failed to execute time-series heatmap query. Check debug log for details.")
            elif heatmap_data:
                # Fill in missing time buckets
                heatmap_data = self._fill_missing_time_buckets(heatmap_data)
                # Calculate the number of unique time buckets and find displayed range
                unique_time_buckets = set()
                min_time = None
                max_time = None
                granularity = self.granularity_options[self.current_granularity_index]
                
                for item in heatmap_data:
                    time_key = TimeUtils.extract_time_buckets(item, granularity)
                    unique_time_buckets.add(time_key)
                    
                    # Track min/max displayed time
                    if min_time is None or time_key < min_time:
                        min_time = time_key
                    if max_time is None or time_key > max_time:
                        max_time = time_key
                
                # Show displayed time range
                if min_time and max_time:
                    yield Static(f"[dim]Displayed time range: {min_time} to {max_time}[/dim]")
                
                # Set width dynamically based on number of time buckets
                # Add some padding and ensure minimum width
                heatmap_width = max(len(unique_time_buckets), 20)
                # Don't make it too narrow, but allow it to be very wide
                heatmap_width = max(heatmap_width, 40)
                
                # Log the calculated width for debugging
                import logging
                logger = logging.getLogger('xtop')
                logger.debug(f"Heatmap width calculated: {heatmap_width} time buckets (unique: {len(unique_time_buckets)})")
                
                # Debug: Check what bucket values we have
                unique_buckets = set()
                for item in heatmap_data:
                    if 'lat_bucket_us' in item:
                        unique_buckets.add(item['lat_bucket_us'])
                logger.debug(f"Peek modal buckets before heatmap: {sorted(unique_buckets)[:10]}")
                
                # Generate time-series heatmap visualization with colors using Rich markup
                config = HeatmapConfig(width=heatmap_width, height=12, use_color=True, use_rich_markup=True)
                heatmap = LatencyHeatmap(config)
                
                # Generate the time-series heatmap
                heatmap_str = heatmap.generate_timeseries_heatmap(heatmap_data, palette='blue')
                
                # Use RichLog for colored output with auto height and horizontal scrolling
                from textual.widgets import RichLog
                heatmap_log = RichLog(highlight=False, markup=True, auto_scroll=False, wrap=False)
                heatmap_log.write(heatmap_str)
                yield heatmap_log
            else:
                yield Static("[dim]No time-series data available for heatmap[/dim]")
                
        except Exception as e:
            # Fallback if heatmap fails
            import logging
            logging.warning(f"Could not generate time-series heatmap: {e}")
            yield Static(f"[red bold]Error:[/red bold] Could not generate heatmap visualization: {str(e)}")
    
    def _fill_missing_time_buckets(self, heatmap_data: List[Dict]) -> List[Dict]:
        """Fill in missing time buckets with zero values"""
        if not heatmap_data:
            return heatmap_data
        
        granularity = self.granularity_options[self.current_granularity_index]
        
        # Use TimeUtils for filling missing buckets
        return TimeUtils.fill_missing_buckets(heatmap_data, granularity)
    
    def _get_missing_time_buckets(self, prev_item: Dict, curr_item: Dict, granularity: str) -> List[Dict]:
        """Get list of missing time buckets between two items"""
        # Delegate to TimeUtils
        return TimeUtils.get_missing_buckets(prev_item, curr_item, granularity)
    
    def action_toggle_granularity(self) -> None:
        """Toggle between different time granularities and refresh heatmap"""
        # Cycle through granularity options
        self.current_granularity_index = (self.current_granularity_index + 1) % len(self.granularity_options)
        
        # Find and refresh the heatmap section
        heatmap_section = self.query_one("#heatmap-section", Container)
        if heatmap_section:
            # Clear current content
            heatmap_section.remove_children()
            
            # Regenerate content with new granularity
            for widget in self._generate_heatmap_content():
                heatmap_section.mount(widget)
    
    def _parse_histogram_data(self) -> List[Tuple[int, int, float, float]]:
        """Parse histogram data string into structured format
        
        Returns:
            List of tuples (bucket_us, count, est_time_s, global_max)
        """
        if not self.histogram_data or self.histogram_data == '-':
            return []
        
        data = []
        try:
            # Limit parsing to prevent memory issues with huge strings
            items = self.histogram_data.split(',')
            max_items = 1000  # Safety limit
            
            for i, item in enumerate(items):
                if i >= max_items:
                    break
                    
                parts = item.split(':')
                if len(parts) >= 4:
                    bucket = int(parts[0])
                    count = int(parts[1])
                    est_time = float(parts[2])
                    global_max = float(parts[3])
                    
                    # Only include non-zero entries
                    if count > 0 or est_time > 0:
                        data.append((bucket, count, est_time, global_max))
        except Exception:
            return []
        
        return sorted(data, key=lambda x: x[0])
    
    def _run_timeseries_query(self) -> Optional[List[Dict]]:
        """Fetch time-series histogram data for the active granularity."""
        granularity = self.granularity_options[self.current_granularity_index]
        return self._provider.fetch_timeseries_histogram(
            column_name=self.column_name,
            where_clause=self.where_clause,
            low_time=self.low_time,
            high_time=self.high_time,
            granularity=granularity,
        )
    
    def _run_histogram_query_and_populate(self, table: DataTable) -> None:
        """Populate the histogram DataTable using the shared provider."""
        try:
            table_data = self._provider.fetch_histogram_table(
                column_name=self.column_name,
                where_clause=self.where_clause,
                low_time=self.low_time,
                high_time=self.high_time,
            )
        except Exception as exc:  # pragma: no cover - protective logging
            logger = logging.getLogger('xtop')
            logger.error("Error in histogram peek query: %s", exc)
            table.add_row(f"Error: {exc}", "-", "-", "-", "-", "-")
            return

        if not table_data.rows:
            table.add_row("No histogram data", "-", "-", "-", "-", "-")
            return

        for row in table_data.rows:
            table.add_row(
                self._format_latency_range(row.bucket_us),
                f"{row.count:>12,}",
                f"{row.est_events_per_s:>14,.0f}",
                f"{row.est_time_s:>12.3f}",
                f"{row.time_pct:>7.1f}%",
                self._build_visual_bar(row.relative_time_ratio, row.est_time_s),
            )

        # Separator row
        table.add_row(
            "─" * 15,
            "─" * 12,
            "─" * 14,
            "─" * 12,
            "─" * 8,
            "─" * 20,
            key="separator",
        )

        table.add_row(
            "TOTAL",
            f"{table_data.total_count:>12,}",
            " " * 14 + "-",
            f"{table_data.total_time_s:>12.3f}",
            f"{100.0:>7.1f}%",
            "",
            key="total",
        )
    
    def _format_latency_range(self, bucket_us: int) -> str:
        """Format bucket value into latency range string, right-aligned"""
        # Use the formatter component and right-align to 15 characters
        formatted = self.formatter.format_latency_range(bucket_us)
        return f"{formatted:>15}"

    @staticmethod
    def _build_visual_bar(relative_ratio: float, est_time: float) -> str:
        """Create a simple bar visualization for the histogram table."""
        width = int(relative_ratio * 20) if relative_ratio > 0 else 0
        width = max(0, min(width, 20))
        if width > 0:
            return "█" * width
        return "▏" if est_time > 0 else ""
    
    def _extract_filter_display(self, where_clause: str) -> str:
        """Extract a readable filter display from WHERE clause"""
        if not where_clause or where_clause == "1=1":
            return ""
        
        # Simple extraction of key=value pairs
        # This is a simplified version - could be enhanced with proper SQL parsing
        filters = []
        parts = where_clause.split(" AND ")
        for part in parts:
            # Clean up parentheses and whitespace
            part = part.strip().strip("()")
            # Only show simple equality filters, skip complex conditions
            if "=" in part and not ">" in part and not "<" in part:
                # Extract column and value
                if "'" in part:
                    # String value
                    col_val = part.split("=", 1)
                    if len(col_val) == 2:
                        col = col_val[0].strip()
                        val = col_val[1].strip().strip("'")
                        # Remove table prefix for display
                        if '.' in col:
                            col = col.split('.')[-1]
                        filters.append(f"{col}={val}")
                else:
                    # Numeric or other value
                    filters.append(part.strip())
        
        # Limit display to first few filters to avoid overflow
        if len(filters) > 3:
            return ", ".join(filters[:3]) + f" (+{len(filters)-3} more)"
        return ", ".join(filters)
