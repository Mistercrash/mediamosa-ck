<?php
/**
 * @file
 * The plugin query object for MediaMosa allows REST calls inside views. It
 * uses the Views 3 plugin option to query another source instead of the normal
 * Drupal database.
 *
 * View will return the featured assets.
 */

class mediamosa_ck_views_rest_asset_get extends mediamosa_ck_views_rest {

  /**
   * Execute the REST call.
   *
   * @param object $view
   *   The view object.
   * @param array $params
   *   Array of view options and settings.
   * @param array $options
   *   Options that can be used for overrides on the view params and REST call
   *   related options.
   */
  protected function do_rest_call($view, $params, array $options = array()) {
    // Total results.
    $view->total_rows = 0;

    // Get the asset_id.
    $asset_id = empty($view->args) ? '' : $view->args[0];

    if (empty($asset_id)) {
      return;
    }

    // Specific for playing.
    $mediafile_id_to_play = empty($view->args[1]) ? '' : $view->args[1];

    // Stills and collections are needed by code.
    $options['show_stills'] = TRUE;
    $options['show_collections'] = TRUE;

    try {
      // Get the asset.
      $xml = MediaMosaCkConnectorWrapper::get_asset($asset_id, $options);
    }
    catch (Exception $e) {
      mediamosa_ck::watchdog_error('Unable to retrieve asset from MediaMosa, message: @message.', array('@message' => $e->getMessage()));
      return;
    }

    $fields = array(
      'viewed', 'played',
      'videotimestamp', 'videotimestampmodified',
      'owner_id', 'group_id',
    );

    // Now process the xml into an object views can work with.
    $item = $xml->xpath('items/item');
    $item = reset($item);
    if (!empty($item)) {
      $object = new stdClass();

      // If mediafile_id_to_play is set, the set it in the object.
      $object->mediafile_id_to_play = $mediafile_id_to_play ? $mediafile_id_to_play : FALSE;

      $object->asset_id = (string) $item->asset_id;
      $object->granted = MediaMosaCkConnectorWrapper::string2bool((string) $item->granted) ? 'TRUE' : 'FALSE';
      $object->is_owner = mediamosa_ck::session_user_id() == $item->owner_id;

      // Other rights.
      $object->access_edit = $object->is_owner;
      $object->access_delete = $object->is_owner;

      // Asset Metadata.
      foreach (mediamosa_ck::get_metadata_fields() as $set => $metadata) {
        foreach ($item->xpath($metadata['xpath']) as $metadata_value) {
          foreach ($metadata_value as $name => $value) {
            // In variable.
            $object->{$set . '.' . (string) $name} = (string) $value;

            // In set.
            $object->{'metadata_' . $set}[(string) $name] = (string) $value;
          }
        }
      }

      // Copy these values.
      foreach ($fields as $field) {
        // @todo need to get default value when value is empty.
        $object->{$field} = isset($item->{$field}) ? (string) $item->{$field} : '';
      }

      // Collection data.
      foreach ($item->xpath('collections/collection') as $collection) {
        $object->collections[(string) $collection->attributes()->id] = array('title' => (string) $collection->title, 'coll_id' => (string) $collection->attributes()->id);
      }

      // Empty by default.
      $object->technical_metadata = array();

      // Mediafiles.
      foreach ($item->xpath('mediafiles/mediafile') as $mediafile) {
        $mediafile = self::xmlobject2array($mediafile);
        $object->mediafiles[(string) $mediafile['mediafile_id']] = $mediafile;

        // Find original and copy its technical metadata.
        if (MediaMosaCkConnectorWrapper::string2bool((string) $mediafile['is_original_file'])) {
          $object->technical_metadata = $mediafile['metadata'];
          $object->mediafile_id_original = $mediafile['mediafile_id'];
        }
      }

      // Get the title value.
      $object->title = mediamosa_ck::get_asset_title($object);

      // Get the description value.
      $object->description = mediamosa_ck::get_asset_description($object);

      // Allow other modules to alter the object.
      drupal_alter('mediamosa_asset_view_object', $object);

      // Although its one object, will put into an array anyway.
      $view->result[] = $object;
    }

    // Assign total number of records to get pager.
    $view->total_rows = (int) $xml->header->item_count_total;
  }

  /**
   * Get the views data for this REST call.
   *
   * @return array
   *   data array.
   */
  public static function get_views_data($title, $class) {
    $data = array(
      'table' => array(
        'group' => t('MediaMosa'),
        'base' => array(
          'field' => 'id',
          'title' => t($title),
          'help' => t('Retrieve a asset using the MediaMosa backend'),
          'query class' => $class,
        ),
      ),
    );

    // Add asset stuff.
    self::get_views_data_asset_common($data);

    // Add asset specific.
    self::get_views_data_asset_specific($data);

    // Owner / group.
    self::get_views_data_owner_stuff($data);

    // Make asset_id contextual.
    $data['asset_id']['argument'] = array(
      'handler' => 'views_handler_argument_string',
    );

    // Add metadata.
    self::get_views_data_metadata($data);

    // Mediafiles.
    $data['mediafiles'] = array(
      'title' => t('Mediafiles'),
      'help' => t('Shows a listing of the mediafiles.'),
      'field' => array(
        'handler' => 'mediamosa_ck_views_field_text_mediafiles',
      ),
    );

    // Collections.
    $data['collections'] = array(
      'title' => t('Collections'),
      'help' => t('Shows a listing of the collection.'),
      'field' => array(
        'handler' => 'mediamosa_ck_views_field_text_collections',
      ),
    );

    // Collections.
    $data['technical_metadata'] = array(
      'title' => t('Technical metadata'),
      'help' => t('Show the technical metadata of the asset.'),
      'field' => array(
        'handler' => 'mediamosa_ck_views_field_text_technical_metadata',
      ),
    );

    // Metadata blocks.
    foreach (mediamosa_ck::get_metadata_fields() as $set => $metadata) {
      // Mediafiles.
      $data['metadata_' . $set] = array(
        'title' => t('Metadata block for ') . $metadata['title'],
        'help' => t('Shows a listing of the metadata.'),
        'field' => array(
          'handler' => 'mediamosa_ck_views_field_text_metadata',
        ),
      );
    }

    return $data;
  }
}
